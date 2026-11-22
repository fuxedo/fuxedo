// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// 16-bit FML implementation: a small header plus packed records
//   [FLDID id][FLDLEN len][payload, len bytes]...
// Capacity bookkeeping lives in the header so tpalloc'd buffers stay
// self-describing on the wire. Only the operation set the classic
// applications use is provided; semantics follow the platform (Fadd
// appends an occurrence, Fchg updates in place or appends when the
// field/occurrence is new, FNOSPACE when the value cannot fit).

#include <fml.h>
#include <fml1632.h>
#include <fml32.h>

#include <stdlib.h>
#include <string.h>

#include "misc.h"

static thread_local int f16_error = 0;
int Ferror;  // ABI-visible; the TLS copy is authoritative
#define F16ERROR(e, msg)                        \
  do {                                        \
    f16_error = e;                            \
    Ferror = e;                               \
    fux::fml32::set_Ferror32(e, "FML16: %s", msg); \
  } while (0)

namespace {

struct fbfr16 {
  FLDLEN buflen;  // capacity of the record area, bytes
  FLDLEN used;    // bytes used by records
  FLDOCC count;   // number of records
  char records[0];
};

constexpr size_t kReclen = sizeof(FLDID) + sizeof(FLDLEN);

fbfr16 *hdr(FBFR *fb) { return reinterpret_cast<fbfr16 *>(fb); }

// locate occurrence occ of id; returns a pointer to the record or
// nullptr, and the end of the last matching record in *lastp
char *scan(FBFR *fb, FLDID id, FLDOCC occ, char **lastp) {
  auto *h = hdr(fb);
  char *p = h->records;
  char *end = h->records + h->used;
  int seen = 0;
  char *last = nullptr;
  while (p < end) {
    auto rid = *reinterpret_cast<FLDID *>(p);
    auto len = *reinterpret_cast<FLDLEN *>(p + sizeof(FLDID));
    if (rid == id) {
      if (seen == occ) {
        if (lastp != nullptr) {
          *lastp = last;
        }
        return p;
      }
      seen++;
      last = p;
    }
    p += kReclen + len;
  }
  if (lastp != nullptr) {
    *lastp = last;
  }
  return nullptr;
}

FLDLEN value_len(int type, const char *value, FLDLEN len) {
  switch (type) {
    case FLD_SHORT:
      return sizeof(short);
    case FLD_LONG:
      return sizeof(long);
    case FLD_CHAR:
      return sizeof(char);
    case FLD_FLOAT:
      return sizeof(float);
    case FLD_DOUBLE:
      return sizeof(double);
    case FLD_STRING:
      return static_cast<FLDLEN>(strlen(value) + 1);
    case FLD_CARRAY:
      return len;
    default:
      return len;
  }
}

int put(FBFR *fb, FLDID id, FLDOCC occ, char *value, FLDLEN vlen,
        bool add) {
  if (fb == nullptr || id == BADFLDID || value == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return -1;
  }
  auto *h = hdr(fb);
  auto type = Fldtype(id);
  if (type > FLD_CARRAY) {
    F16ERROR(FBADFLD, "bad field type");
    return -1;
  }
  auto want = value_len(type, value, vlen);

  if (!add) {
    char *at = scan(fb, id, occ, nullptr);
    if (at != nullptr) {
      auto old = *reinterpret_cast<FLDLEN *>(at + sizeof(FLDID));
      if (want <= old) {
        // shrink in place, collapsing the slack
        char *val = at + kReclen;
        memmove(val + want, val + old, (h->records + h->used) - (val + old));
        h->used = static_cast<FLDLEN>(h->used - (old - want));
        memcpy(val, value, want);
        *reinterpret_cast<FLDLEN *>(at + sizeof(FLDID)) = want;
        return 1;
      }
      // growing: drop the old occurrence and re-insert below
      Fdel(fb, id, occ);
    } else if (occ > Fcount(fb, id)) {
      // like the platform, a change beyond the last occurrence
      // materializes the gap with default (zero) values first
      static const char zeros[sizeof(double)] = {0};
      for (FLDOCC o = Fcount(fb, id); o < occ; o++) {
        if (put(fb, id, o, const_cast<char *>(zeros), 0, false) == -1) {
          return -1;
        }
      }
    }
  }

  if (h->used + kReclen + want > h->buflen) {
    F16ERROR(FNOSPACE, "no space in fielded buffer");
    return -1;
  }
  // occurrences of a field stay grouped in insertion order: append
  // after the field's last record, or at the end for a new field
  char *ins = h->records + h->used;
  auto n = Fcount(fb, id);
  if (n > 0) {
    char *last = scan(fb, id, n - 1, nullptr);
    ins = last + kReclen +
          *reinterpret_cast<FLDLEN *>(last + sizeof(FLDID));
  }
  size_t tail = static_cast<size_t>((h->records + h->used) - ins);
  memmove(ins + kReclen + want, ins, tail);
  memcpy(ins, &id, sizeof(id));
  memcpy(ins + sizeof(id), &want, sizeof(want));
  memcpy(ins + kReclen, value, want);
  h->used = static_cast<FLDLEN>(h->used + kReclen + want);
  h->count++;
  return 1;
}

}  // namespace

int Fadd(FBFR *fb, FLDID fieldid, char *value, FLDLEN len) {
  return put(fb, fieldid, 0, value, len, true);
}

int Fchg(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value, FLDLEN len) {
  return put(fb, fieldid, occ, value, len, false);
}

int Fdel(FBFR *fb, FLDID fieldid, FLDOCC occ) {
  if (fb == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return -1;
  }
  auto *h = hdr(fb);
  char *at = scan(fb, fieldid, occ, nullptr);
  if (at == nullptr) {
    F16ERROR(FNOTPRES, "field not present");
    return -1;
  }
  auto len = *reinterpret_cast<FLDLEN *>(at + sizeof(FLDID));
  memmove(at, at + kReclen + len,
          static_cast<size_t>((h->records + h->used) - (at + kReclen + len)));
  h->used = static_cast<FLDLEN>(h->used - (kReclen + len));
  h->count--;
  return 1;
}

FLDOCC Fdelall(FBFR *fb, FLDID fieldid) {
  // pinned on Oracle 12.2.2 and 22.1: Fdelall deletes every occurrence
  // but answers 1 (not the count), and -1 with Ferror=FNOTPRES when
  // the field is absent
  if (fb == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return -1;
  }
  if (Fcount(fb, fieldid) == 0) {
    F16ERROR(FNOTPRES, "field not present");
    return -1;
  }
  while (Fdel(fb, fieldid, 0) != -1) {
  }
  f16_error = 0;
  Ferror = 0;
  return 1;
}

char *Ffind(FBFR *fb, FLDID fieldid, FLDOCC occ, FLDLEN *len) {
  if (fb == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return nullptr;
  }
  char *at = scan(fb, fieldid, occ, nullptr);
  if (at == nullptr) {
    F16ERROR(FNOTPRES, "field not present");
    return nullptr;
  }
  auto vlen = *reinterpret_cast<FLDLEN *>(at + sizeof(FLDID));
  if (len != nullptr) {
    *len = vlen;
  }
  return at + kReclen;
}

int Fget(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value, FLDLEN *len) {
  FLDLEN vlen = 0;
  auto *v = Ffind(fb, fieldid, occ, &vlen);
  if (v == nullptr) {
    return -1;
  }
  if (value == nullptr || len == nullptr || *len < vlen) {
    F16ERROR(FNOSPACE, "value buffer too small");
    return -1;
  }
  memcpy(value, v, vlen);
  *len = vlen;
  return 1;
}

char *Fvals(FBFR *fb, FLDID fieldid, FLDOCC occ) {
  return Ffind(fb, fieldid, occ, nullptr);
}

long Fvall(FBFR *fb, FLDID fieldid, FLDOCC occ) {
  auto *v = Ffind(fb, fieldid, occ, nullptr);
  if (v == nullptr) {
    return -1;
  }
  switch (Fldtype(fieldid)) {
    case FLD_SHORT:
      return *reinterpret_cast<short *>(v);
    case FLD_LONG:
      return *reinterpret_cast<long *>(v);
    case FLD_CHAR:
      return static_cast<unsigned char>(*v);
    case FLD_FLOAT:
      return static_cast<long>(*reinterpret_cast<float *>(v));
    case FLD_DOUBLE:
      return static_cast<long>(*reinterpret_cast<double *>(v));
    case FLD_STRING:
      return atol(v);
    default:
      F16ERROR(FTYPERR, "no long conversion for the field type");
      return -1;
  }
}

long Fsizeof(FBFR *fb) {
  if (fb == nullptr) {
    return -1;
  }
  return static_cast<long>(hdr(fb)->buflen);
}

long Fused(FBFR *fb) {
  if (fb == nullptr) {
    return -1;
  }
  return hdr(fb)->used;
}

FLDOCC Fcount(FBFR *fb, FLDID fieldid) {
  if (fb == nullptr) {
    return -1;
  }
  auto *h = hdr(fb);
  char *p = h->records;
  char *end = h->records + h->used;
  FLDOCC n = 0;
  while (p < end) {
    if (*reinterpret_cast<FLDID *>(p) == fieldid) {
      n++;
    }
    p += kReclen + *reinterpret_cast<FLDLEN *>(p + sizeof(FLDID));
  }
  return n;
}

FLDOCC Foccur(FBFR *fb, FLDID fieldid) { return Fcount(fb, fieldid); }

int Finit(FBFR *fb, FLDLEN buflen) {
  if (fb == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return -1;
  }
  auto *h = hdr(fb);
  if (buflen > 0) {
    h->buflen = buflen;
  }
  h->used = 0;
  h->count = 0;
  return 1;
}

FBFR *Falloc(FLDOCC /*flds*/, FLDLEN len) {
  auto total = sizeof(fbfr16) + (len == 0 ? 512 : len);
  auto *fb = static_cast<FBFR *>(calloc(1, total));
  if (fb == nullptr) {
    F16ERROR(FMALLOC, "out of memory");
    return nullptr;
  }
  hdr(fb)->buflen = static_cast<FLDLEN>(total - sizeof(fbfr16));
  return fb;
}

void Ffree(FBFR *fb) { free(fb); }

const char *Fstrerror(int err) {
  switch (err) {
    case 0:
      return "success";
    case FALIGNERR:
      return "fielded buffer not aligned";
    case FNOTFLD:
      return "buffer not fielded";
    case FNOSPACE:
      return "no space in fielded buffer";
    case FNOTPRES:
      return "field not present";
    case FBADFLD:
      return "bad field identifier";
    case FTYPERR:
      return "bad field type";
    case FEUNIX:
      return "operating system error";
    case FBADNAME:
      return "bad field name";
    case FMALLOC:
      return "memory allocation failed";
    case FOPEN:
      return "open failed";
    case FSYNTAX:
      return "bad syntax";
    case FEINVAL:
      return "invalid argument";
    default:
      return "unknown FML16 error";
  }
}

FLDID Fldid(char *name) {
  if (name == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return BADFLDID;
  }
  // headers produced by mkfldhdr carry numeric ids; name lookup goes
  // through the shared field tables (same names and numbers), then
  // the system field list the platform resolves without tables
  auto id32 = Fldid32(name);
  if (id32 != 0) {
    return Fmkfldid(Fldtype32(id32), static_cast<long>(Fldno32(id32)));
  }
  static const struct {
    const char *name;
    FLDID id;
  } system_fields[] = {
      {"INITMSK", 40961},   {"CURSID", 8194},     {"CURSOC", 8195},
      {"LEVKEY", 40964},    {"STATLIN", 40965},   {"FORMNAM", 40966},
      {"UPDTMOD", 7},       {"SRVCNM", 40968},    {"NEWFORM", 40969},
      {"CHGATTS", 49162},   {"USYS1FLD", 40971},  {"USYS2FLD", 40972},
      {"USYS3FLD", 40973},  {"USYS4FLD", 49166},  {"USYS5FLD", 49167},
      {"USYS6FLD", 49168},  {"DESTSRVC", 40977},  {"MODS", 49170},
      {"VALONENTRY", 40979},
  };
  for (auto &f : system_fields) {
    if (strcmp(name, f.name) == 0) {
      return f.id;
    }
  }
  F16ERROR(FBADNAME, "unknown field name");
  return BADFLDID;
}

// 16<->32 conversion, the platform's in-place shape: the caller
// provides the destination buffer and receives a status
int F16to32(FBFR32 *out, FBFR *fb16) {
  if (out == nullptr || fb16 == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return -1;
  }
  auto *h = hdr(fb16);
  char *p = h->records;
  char *end = h->records + h->used;
  while (p < end) {
    auto id = *reinterpret_cast<FLDID *>(p);
    auto len = *reinterpret_cast<FLDLEN *>(p + sizeof(FLDID));
    auto id32 = Fmkfldid32(Fldtype(id), Fldno(id));
    if (Fadd32(out, id32, p + kReclen, len) == -1) {
      return -1;
    }
    p += kReclen + len;
  }
  return 1;
}

int F32to16(FBFR *out, FBFR32 *fb32) {
  if (out == nullptr || fb32 == nullptr) {
    F16ERROR(FEINVAL, "bad argument");
    return -1;
  }
  auto id = static_cast<FLDID32>(0);
  FLDOCC32 occ = 0;
  while (Fnext32(fb32, &id, &occ, nullptr, nullptr) == 1) {
    FLDLEN32 len = 0;
    auto *v = Ffind32(fb32, id, occ, &len);
    if (v == nullptr ||
        Fadd(out, Fmkfldid(Fldtype32(id), Fldno32(id)),
             const_cast<char *>(v), static_cast<FLDLEN>(len)) == -1) {
      return -1;
    }
  }
  return 1;
}

int CFadd(FBFR *fb, FLDID fieldid, char *value, FLDLEN len, int usrdtyp) {
  (void)usrdtyp;
  return Fadd(fb, fieldid, value, len);
}

int CFchg(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value, FLDLEN len,
          int usrdtyp) {
  (void)usrdtyp;
  return Fchg(fb, fieldid, occ, value, len);
}

char *CFfind(FBFR *fb, FLDID fieldid, FLDOCC occ, FLDLEN *len,
             int usrdtyp) {
  (void)usrdtyp;
  return Ffind(fb, fieldid, occ, len);
}

int CFget(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value, FLDLEN *len,
          int usrdtyp) {
  (void)usrdtyp;
  return Fget(fb, fieldid, occ, value, len);
}
