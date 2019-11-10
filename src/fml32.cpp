// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "fbfr32.h"

namespace fux {
namespace fml32 {

static thread_local int Ferror32_ = 0;

static thread_local char Flasterr32_[1024] = {0};

void set_Ferror32(int err, const char *fmt, ...) {
  Ferror32_ = err;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(Flasterr32_, sizeof(Flasterr32_), fmt, ap);
  va_end(ap);
}

void reset_Ferror32() {
  Ferror32_ = 0;
  Flasterr32_[0] = '\0';
}
}  // namespace fml32
}  // namespace fux

int *_tls_Ferror32() { return &fux::fml32::Ferror32_; }

static const char *Fstrerror32_(int err) {
  switch (err) {
    case FALIGN:
      return "Fielded buffer not aligned";
    case FNOTFLD:
      return "Buffer not fielded";
    case FNOSPACE:
      return "No space in fielded buffer";
    case FNOTPRES:
      return "Field not present";
    case FBADFLD:
      return "Unknown field number or type";
    case FTYPERR:
      return "Illegal field type";
    case FEUNIX:
      return "case UNIX system call error";
    case FBADNAME:
      return "Unknown field name";
    case FMALLOC:
      return "malloc failed";
    case FSYNTAX:
      return "Bad syntax in Boolean expression";
    case FFTOPEN:
      return "Cannot find or open field table";
    case FFTSYNTAX:
      return "Syntax error in field table";
    case FEINVAL:
      return "Invalid argument to function";
    case FBADTBL:
      return "Destructive concurrent access to field table";
    case FBADVIEW:
      return "Cannot find or get view";
    case FVFSYNTAX:
      return "Syntax error in viewfile";
    case FVFOPEN:
      return "Cannot find or open viewfile";
    case FBADACM:
      return "ACM contains negative value";
    case FNOCNAME:
      return "cname not found";
    case FEBADOP:
      return "Invalid field type";
    case FNOTRECORD:
      return "Invalid record type";
    case FRFSYNTAX:
      return "Syntax error in recordfile";
    case FRFOPEN:
      return "Cannot find or open recordfile";
    case FBADRECORD:
      return "Cannot find or get record";
    default:
      return "?";
  }
}

char *Fstrerror32(int err) { return const_cast<char *>(Fstrerror32_(err)); }

FLDID32 Fmkfldid32(int type, FLDID32 num) {
  return Fbfr32fields::Fmkfldid32(type, num);
}

int Fldtype32(FLDID32 fieldid) { return Fbfr32fields::Fldtype32(fieldid); }

long Fldno32(FLDID32 fieldid) { return Fbfr32fields::Fldno32(fieldid); }

static Fbfr32fields Fbfr32fields_;

char *Fname32(FLDID32 fieldid) { return Fbfr32fields_.name(fieldid); }

FLDID32 Fldid32(char *name) { return Fbfr32fields_.fldid(name); }

void Fidnm_unload32() { return Fbfr32fields_.idnm_unload(); }

void Fnmid_unload32() { return Fbfr32fields_.nmid_unload(); }

////////////////////////////////////////////////////////////////////////////

long Fneeded32(FLDOCC32 F, FLDLEN32 V) { return Fbfr32::needed(F, V); }

FBFR32 *Falloc32(FLDOCC32 F, FLDLEN32 V) {
  long buflen = Fneeded32(F, V);
  FBFR32 *fbfr = (FBFR32 *)malloc(buflen);
  Finit32(fbfr, buflen);
  return fbfr;
}

#define FBFR32_CHECK(err, fbfr)                              \
  do {                                                       \
    if (fbfr == nullptr) {                                   \
      FERROR(FNOTFLD, "fbfr is NULL");                       \
      return err;                                            \
    }                                                        \
    if ((reinterpret_cast<intptr_t>(fbfr) & (8 - 1)) != 0) { \
      FERROR(FNOTFLD, "fbfr is aligned");                    \
      return err;                                            \
    }                                                        \
  } while (false)

#define FLDID32_CHECK(err, fldid)                                           \
  do {                                                                      \
    if (!Fbfr32fields::valid_fldtype32(Fbfr32fields::Fldtype32(fieldid))) { \
      FERROR(FTYPERR, "fldid of unknown type");                             \
      return err;                                                           \
    }                                                                       \
    if (fldid == BADFLDID) {                                                \
      FERROR(FBADFLD, "fldid is 0");                                        \
      return err;                                                           \
    }                                                                       \
  } while (false)

int Finit32(FBFR32 *fbfr, FLDLEN32 buflen) {
  FBFR32_CHECK(-1, fbfr);
  if (buflen < Fneeded32(0, 0)) {
    FERROR(FNOSPACE, "%d is less than minimum requirement", buflen);
  }
  return fbfr->init(buflen);
}

FBFR32 *Frealloc32(FBFR32 *fbfr, FLDOCC32 F, FLDLEN32 V) {
  FBFR32_CHECK(nullptr, fbfr);

  long buflen = Fneeded32(F, V);
  if (buflen < Fused32(fbfr)) {
    FERROR(FNOSPACE, "%ld is less than current size", buflen);
    return nullptr;
  }

  fbfr = (FBFR32 *)realloc(fbfr, buflen);
  fbfr->reinit(buflen);
  return fbfr;
}

int Ffree32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  free(fbfr);
  return 0;
}

long Fsizeof32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->size();
}

long Fused32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->used();
}

long Funused32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->unused();
}

long Fidxused32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return 0;
}

int Findex32(FBFR32 *fbfr, FLDOCC32 intvl __attribute__((unused))) {
  FBFR32_CHECK(-1, fbfr);
  return 0;
}

int Funindex32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return 0;
}

int Frstrindex32(FBFR32 *fbfr, FLDOCC32 numidx __attribute__((unused))) {
  FBFR32_CHECK(-1, fbfr);
  return 0;
}

int Fchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *value,
           FLDLEN32 len) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->chg(fieldid, oc, value, len);
}

int Fadd32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  auto oc = fbfr->occur(fieldid);
  return fbfr->chg(fieldid, oc, value, len);
}

char *Ffind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len) {
  FBFR32_CHECK(nullptr, fbfr);
  FLDID32_CHECK(nullptr, fieldid);
  return fbfr->find(fieldid, oc, len);
}
char *Ffindlast32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 *oc, FLDLEN32 *len) {
  FBFR32_CHECK(nullptr, fbfr);
  FLDID32_CHECK(nullptr, fieldid);
  return fbfr->findlast(fieldid, oc, len);
}
int Fpres32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->pres(fieldid, oc);
}

FLDOCC32 Foccur32(FBFR32 *fbfr, FLDID32 fieldid) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->occur(fieldid);
}

long Flen32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->len(fieldid, oc);
}

int Fprint32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->fprint(stdout);
}

int Ffprint32(FBFR32 *fbfr, FILE *iop) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->fprint(iop);
}

int Fdel32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->del(fieldid, oc);
}

int Fdelall32(FBFR32 *fbfr, FLDID32 fieldid) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->delall(fieldid);
}

int Fget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc,
           FLDLEN32 *maxlen) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->get(fieldid, oc, loc, maxlen);
}

int Fnext32(FBFR32 *fbfr, FLDID32 *fieldid, FLDOCC32 *oc, char *value,
            FLDLEN32 *len) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->next(fieldid, oc, value, len);
}
int Fcpy32(FBFR32 *dest, FBFR32 *src) {
  FBFR32_CHECK(-1, dest);
  FBFR32_CHECK(-1, src);
  return dest->cpy(src);
}
char *Fgetalloc32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc,
                  FLDLEN32 *extralen) {
  FBFR32_CHECK(nullptr, fbfr);
  FLDID32_CHECK(nullptr, fieldid);
  return fbfr->getalloc(fieldid, oc, extralen);
}
int Fdelete32(FBFR32 *fbfr, FLDID32 *fieldid) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->xdelete(fieldid);
}
int Fproj32(FBFR32 *fbfr, FLDID32 *fieldid) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->proj(fieldid);
}
int Fprojcpy32(FBFR32 *dest, FBFR32 *src, FLDID32 *fieldid) {
  FBFR32_CHECK(-1, dest);
  FBFR32_CHECK(-1, src);
  return dest->projcpy(src, fieldid);
}
int Fupdate32(FBFR32 *dest, FBFR32 *src) {
  FBFR32_CHECK(-1, dest);
  FBFR32_CHECK(-1, src);
  return dest->update(src);
}
int Fjoin32(FBFR32 *dest, FBFR32 *src) {
  FBFR32_CHECK(-1, dest);
  FBFR32_CHECK(-1, src);
  return dest->join(src);
}
int Fojoin32(FBFR32 *dest, FBFR32 *src) {
  FBFR32_CHECK(-1, dest);
  FBFR32_CHECK(-1, src);
  return dest->ojoin(src);
}
long Fchksum32(FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->chksum();
}

char *Ftypcvt32(FLDLEN32 *tolen, int totype, char *fromval, int fromtype,
                FLDLEN32 fromlen) {
  thread_local std::string toval;

  FLDLEN32 dummy;
  if (tolen == nullptr) {
    tolen = &dummy;
  }

#define COPY_N(what)                                                 \
  auto to = what;                                                    \
  toval.resize(sizeof(to));                                          \
  std::copy_n(reinterpret_cast<char *>(&to), sizeof(to), &toval[0]); \
  *tolen = sizeof(to)

#define TYPCVT(what)                                         \
  auto from = what;                                          \
  if (totype == FLD_SHORT) {                                 \
    COPY_N(static_cast<short>(from));                        \
  } else if (totype == FLD_CHAR) {                           \
    COPY_N(static_cast<char>(from));                         \
  } else if (totype == FLD_FLOAT) {                          \
    COPY_N(static_cast<float>(from));                        \
  } else if (totype == FLD_LONG) {                           \
    COPY_N(static_cast<long>(from));                         \
  } else if (totype == FLD_DOUBLE) {                         \
    COPY_N(static_cast<double>(from));                       \
  } else if (totype == FLD_STRING || totype == FLD_CARRAY) { \
    toval = std::to_string(from);                            \
    *tolen = toval.size() + 1;                               \
  } else {                                                   \
    FERROR(FEBADOP, "");                                     \
    return nullptr;                                          \
  }

#define TYPCVTS                                              \
  if (totype == FLD_SHORT) {                                 \
    COPY_N(static_cast<short>(atol(from.c_str())));          \
  } else if (totype == FLD_CHAR) {                           \
    COPY_N(static_cast<char>(from[0]));                      \
  } else if (totype == FLD_FLOAT) {                          \
    COPY_N(static_cast<float>(atof(from.c_str())));          \
  } else if (totype == FLD_LONG) {                           \
    COPY_N(static_cast<long>(atol(from.c_str())));           \
  } else if (totype == FLD_DOUBLE) {                         \
    COPY_N(static_cast<double>(atof(from.c_str())));         \
  } else if (totype == FLD_STRING || totype == FLD_CARRAY) { \
    toval = from;                                            \
    *tolen = from.size() + 1;                                \
  } else {                                                   \
    FERROR(FEBADOP, "");                                     \
    return nullptr;                                          \
  }

  if (fromtype == FLD_SHORT) {
    TYPCVT(*reinterpret_cast<short *>(fromval));
  } else if (fromtype == FLD_CHAR) {
    if (totype == FLD_STRING || totype == FLD_CARRAY) {
      toval.resize(1);
      toval[0] = *fromval;
      *tolen = toval.size() + 1;
    } else {
      TYPCVT(*reinterpret_cast<char *>(fromval));
    }
  } else if (fromtype == FLD_FLOAT) {
    TYPCVT(*reinterpret_cast<float *>(fromval));
  } else if (fromtype == FLD_LONG) {
    TYPCVT(*reinterpret_cast<long *>(fromval));
  } else if (fromtype == FLD_DOUBLE) {
    TYPCVT(*reinterpret_cast<double *>(fromval));
  } else if (fromtype == FLD_STRING) {
    auto from = std::string(fromval);
    TYPCVTS;
  } else if (fromtype == FLD_CARRAY) {
    auto from = std::string(fromval, fromlen);
    TYPCVTS;
  } else {
    FERROR(FEBADOP, "");
    return nullptr;
  }
  return &toval[0];
}

FLDOCC32 Ffindocc32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  return fbfr->findocc(fieldid, value, len);
}

int Fconcat32(FBFR32 *dest, FBFR32 *src) {
  FBFR32_CHECK(-1, dest);
  FBFR32_CHECK(-1, src);
  return dest->concat(src);
}

int CFadd32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len,
            int type) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);

  FLDLEN32 flen;
  auto cvtvalue = Ftypcvt32(&flen, Fldtype32(fieldid), value, type, len);
  if (cvtvalue == nullptr) {
    return -1;
  }
  auto oc = fbfr->occur(fieldid);
  return fbfr->chg(fieldid, oc, cvtvalue, flen);
}

int CFchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *value,
            FLDLEN32 len, int type) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);

  FLDLEN32 flen;
  auto cvtvalue = Ftypcvt32(&flen, Fldtype32(fieldid), value, type, len);
  if (cvtvalue == nullptr) {
    return -1;
  }
  return fbfr->chg(fieldid, oc, cvtvalue, flen);
}
FLDOCC32
CFfindocc32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len,
            int type) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);
  FLDLEN32 flen;
  auto cvtvalue = Ftypcvt32(&flen, Fldtype32(fieldid), value, type, len);
  if (cvtvalue == nullptr) {
    return -1;
  }
  return fbfr->findocc(fieldid, cvtvalue, flen);
}

char *CFfind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len,
               int type) {
  FBFR32_CHECK(nullptr, fbfr);
  FLDID32_CHECK(nullptr, fieldid);

  FLDLEN32 local_flen;
  if (len == nullptr) {
    len = &local_flen;
  }
  auto res = fbfr->find(fieldid, oc, len);
  if (res == nullptr) {
    return nullptr;
  }
  return Ftypcvt32(len, type, res, Fldtype32(fieldid), *len);
}

int CFget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *buf,
            FLDLEN32 *len, int type) {
  FBFR32_CHECK(-1, fbfr);
  FLDID32_CHECK(-1, fieldid);

  FLDLEN32 rlen = 0;
  FLDLEN32 local_flen;
  if (len == nullptr) {
    len = &local_flen;
  } else {
    rlen = *len;
  }
  auto res = CFfind32(fbfr, fieldid, oc, len, type);
  if (res == nullptr) {
    return -1;
  }
  if (rlen != 0 && *len > rlen) {
    FERROR(FNOSPACE, "");
    return -1;
  }
  std::copy_n(res, *len, buf);
  return 0;
}

int Fextread32(FBFR32 *fbfr, FILE *iop) {
  FBFR32_CHECK(-1, fbfr);
  auto p = extreader(iop);
  auto f = p.parse();
  if (!f) {
    return -1;
  }
  return fbfr->cpy(f.get());
}

int Fwrite32(FBFR32 *fbfr, FILE *iop) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->write(iop);
}

int Fread32(FBFR32 *fbfr, FILE *iop) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->read(iop);
}

char *Ffinds32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  return CFfind32(fbfr, fieldid, oc, nullptr, FLD_STRING);
}

int Fgets32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *buf) {
  return CFget32(fbfr, fieldid, oc, buf, nullptr, FLD_STRING);
}
