#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <string>

#include <algorithm>
#include <atomic>
#include <memory>

#include <fml32.h>
#include <regex.h>
#include "extreader.h"

#include "fbfr32fld.h"
#include "misc.h"

namespace fux {
namespace fml32 {
void set_Ferror32(int err, const char *fmt, ...);
void reset_Ferror32();
}  // namespace fml32
}  // namespace fux

#define FERROR(err, fmt, args...)                                          \
  fux::fml32::set_Ferror32(err, "%s() in %s:%d: " fmt, __func__, __FILE__, \
                           __LINE__, ##args)

static unsigned int crc32b(unsigned char *data, size_t len) {
  unsigned crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    auto byte = data[i];
    crc = crc ^ byte;
    for (auto j = 7; j >= 0; j--) {
      auto mask = -(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~crc;
}

struct fieldhead {
  FLDID32 fieldid;
};

struct field8b : fieldhead {
  field8b(FLDID32 fieldid) : fieldhead{fieldid} {}
  bool operator<(const field8b &other) const { return fieldid < other.fieldid; }
  union {
    char c;
    short s;
    float f;
    char data[4];
  };
};
static_assert(sizeof(field8b) == 8, "Small fixed fields must be 8 bytes");

struct field16b : fieldhead {
  field16b(FLDID32 fieldid) : fieldhead{fieldid} {}
  bool operator<(const field16b &other) const {
    return fieldid < other.fieldid;
  }
  union {
    long l;
    double d;
    char data[8];
  } __attribute__((aligned(8)));
};
static_assert(sizeof(field16b) == 16, "Large fixed fields must be 16 bytes");

struct fieldn : fieldhead {
  FLDLEN32 flen;
  char data[] __attribute__((aligned(8)));

  FLDLEN32 size() { return size(flen); }
  static constexpr FLDLEN32 size(FLDLEN32 ilen) {
    if (ilen & 7) {
      ilen += 8 - (ilen & 7);
    }
    return ilen;
  }
};
static_assert(sizeof(fieldn) == 8, "Variable field header must be 8 bytes");

struct Fbfr32 {
 private:
  Fbfr32() = delete;
  Fbfr32(const Fbfr32 &) = delete;
  Fbfr32 &operator=(const Fbfr32 &) = delete;

 public:
  static constexpr long needed(FLDOCC32 F, FLDLEN32 V) {
    return F * (sizeof(FLDID32) + sizeof(uint32_t) + V) + sizeof(Fbfr32);
  }

  int init(FLDLEN32 buflen) {
    if (buflen < min_size()) {
      FERROR(FNOSPACE, "");
      return -1;
    }
    size_ = buflen - min_size();
    len_ = 0;

    memset(offsets_, 0, sizeof(offsets_));
    return 0;
  }
  void reinit(FLDLEN32 buflen) { size_ = buflen - min_size(); }
  void finit() {}

  long size() const { return size_ + min_size(); }
  long used() const { return len_ + min_size(); }
  long unused() const { return size_ - len_; }

  long chksum() {
    return crc32b(reinterpret_cast<unsigned char *>(data_), len_);
  }

  int cpy(FBFR32 *src) {
    if (size() < src->used()) {
      FERROR(FNOSPACE, "");
      return -1;
    }
    len_ = src->len_;
    std::copy_n(src->offsets_, max_offset_, offsets_);
    std::copy_n(src->data_, src->len_, data_);
    return 0;
  }

  int write(FILE *iop) {
    // Writes out everything starting with len
    auto n = used() - sizeof(size_);
    if (fwrite(&len_, 1, n, iop) != n) {
      FERROR(FEUNIX, "");
      return -1;
    }
    return 0;
  }

  int read(FILE *iop) {
    uint32_t len;
    if (fread(&len, 1, sizeof(len), iop) != sizeof(len)) {
      FERROR(FEUNIX, "");
      return -1;
    }
    if (len > size_) {
      FERROR(FNOSPACE, "");
      return -1;
    }

    len_ = len;
    auto n = used() - sizeof(size_) - sizeof(len_);
    if (fread(reinterpret_cast<char *>(&len_) + sizeof(len_), 1, n, iop) != n) {
      FERROR(FEUNIX, "");
      return -1;
    }
    return 0;
  }

  int chg(FLDID32 fieldid, FLDOCC32 oc, char *value, FLDLEN32 flen) {
    auto type = Fldtype32(fieldid);
    auto klass = fldclass(fieldid);

    // Fill in default values
    flen = value_len(type, value, flen);

    ssize_t need;
    if (klass == FIELD8) {
      need = sizeof(field8b);
    } else if (klass == FIELD16) {
      need = sizeof(field16b);
    } else if (klass == FIELDN) {
      need = sizeof(fieldn) + fieldn::size(flen);
    } else {
      __builtin_unreachable();
    }

    auto field = where(fieldid, oc);
    size_t used = 0;
    if (field == nullptr) {
      field = end(type);
    } else if (field->fieldid == fieldid) {
      if (klass == FIELD8 || klass == FIELD16) {
        need = 0;
      } else if (klass == FIELDN) {
        used = sizeof(fieldn) + reinterpret_cast<fieldn *>(field)->size();
        need -= used;
      } else {
        __builtin_unreachable();
      }
    }

    if (need != 0) {
      if (len_ + need > size_) {
        FERROR(FNOSPACE, "");
        return -1;
      }

      auto ptr = reinterpret_cast<char *>(field);
      // std::copy(ptr, data_ + len_, ptr + need);
      // copy does not work for overlaps?
      if (need < 0) {
        memmove(ptr + need + used, ptr + used, (data_ + len_) - (ptr + used));
      } else {
        memmove(ptr + need, ptr, (data_ + len_) - ptr);
      }
    }

    field->fieldid = fieldid;

    if (klass == FIELD8) {
      auto f = reinterpret_cast<field8b *>(field);
      // To avoid junk bytes in the buffer
      memset(f->data, 0x0, sizeof(f->data));
      std::copy_n(value, flen, reinterpret_cast<field8b *>(field)->data);
    } else if (klass == FIELD16) {
      std::copy_n(value, flen, reinterpret_cast<field16b *>(field)->data);
    } else if (klass == FIELDN) {
      auto f = reinterpret_cast<fieldn *>(field);
      f->flen = flen;
      std::copy_n(value, flen, f->data);
      if (type == FLD_FML32) {
        FBFR32 *fbfr = reinterpret_cast<FBFR32 *>(f->data);
        fbfr->size_ = fbfr->len_;
      }
    } else {
      __builtin_unreachable();
    }

    shift(type, need);
    return 0;
  }

  FLDOCC32 findocc(FLDID32 fieldid, char *value, FLDLEN32 len) {
    auto it = where(fieldid, 0);
    FLDOCC32 oc = 0;
    auto type = Fldtype32(fieldid);

    regex_t re;
    // at scope exit delete regex if we created it
    std::unique_ptr<regex_t, decltype(&::regfree)> guard(nullptr, &::regfree);
    if (type == FLD_STRING && len != 0) {
      if (regcomp(&re, value, REG_EXTENDED | REG_NOSUB) != 0) {
        FERROR(FEINVAL, "");
        return -1;
      }
      guard = std::unique_ptr<regex_t, decltype(&::regfree)>(&re, &::regfree);
    }

    while (it != nullptr && it->fieldid == fieldid) {
      if (type == FLD_SHORT) {
        if (reinterpret_cast<field8b *>(it)->s ==
            *reinterpret_cast<short *>(value)) {
          return oc;
        }
      } else if (type == FLD_CHAR) {
        if (reinterpret_cast<field8b *>(it)->c ==
            *reinterpret_cast<char *>(value)) {
          return oc;
        }
      } else if (type == FLD_FLOAT) {
        if (reinterpret_cast<field8b *>(it)->f ==
            *reinterpret_cast<float *>(value)) {
          return oc;
        }
      } else if (type == FLD_LONG) {
        if (reinterpret_cast<field16b *>(it)->l ==
            *reinterpret_cast<long *>(value)) {
          return oc;
        }
      } else if (type == FLD_DOUBLE) {
        if (reinterpret_cast<field16b *>(it)->d ==
            *reinterpret_cast<double *>(value)) {
          return oc;
        }
      } else if (type == FLD_STRING) {
        auto field = reinterpret_cast<fieldn *>(it);
        if (len == 0) {
          if (strcmp(field->data, value) == 0) {
            return oc;
          }
        } else {
          if (regexec(&re, field->data, 0, nullptr, 0) == 0) {
            return oc;
          }
        }

      } else if (type == FLD_CARRAY) {
        auto field = reinterpret_cast<fieldn *>(it);
        if (field->flen == len && memcmp(field->data, value, len) == 0) {
          return oc;
        }
      }
      oc++;
      it = next_(it);
    }
    FERROR(FNOTPRES, "");
    return -1;
  }

  char *find(FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *flen) {
    auto field = where(fieldid, oc);
    if (field == nullptr || field->fieldid != fieldid) {
      FERROR(FNOTPRES, "");
      return nullptr;
    }

    if (flen != nullptr) {
      *flen = flength(field);
    }
    return fvalue(field);
  }

  char *findlast(FLDID32 fieldid, FLDOCC32 *oc, FLDLEN32 *flen) {
    auto lastoc = occur(fieldid);
    if (lastoc == 0) {
      FERROR(FNOTPRES, "");
      return nullptr;
    }

    lastoc -= 1;
    auto field = where(fieldid, lastoc);

    if (oc != nullptr) {
      *oc = lastoc;
    }
    if (flen != nullptr) {
      *flen = flength(field);
    }
    return fvalue(field);
  }

  int pres(FLDID32 fieldid, FLDOCC32 oc) {
    auto field = where(fieldid, oc);
    if (field == nullptr || field->fieldid != fieldid) {
      return 0;
    }
    return 1;
  }

  FLDOCC32 occur(FLDID32 fieldid) {
    auto field = where(fieldid, 0);
    FLDOCC32 oc = 0;
    while (field != nullptr && field->fieldid == fieldid) {
      oc++;
      field = next_(field);
    }
    return oc;
  }

  int next(FLDID32 *fieldid, FLDOCC32 *oc, char *value, FLDLEN32 *len) {
    if (fieldid == nullptr || oc == nullptr) {
      FERROR(FEINVAL, "");
      return -1;
    }

    auto it = reinterpret_cast<fieldhead *>(data_);
    auto end = reinterpret_cast<fieldhead *>(data_ + len_);

    if (*fieldid != BADFLDID) {
      it = where(*fieldid, *oc);
      if (it != nullptr && it < end) {
        it = next_(it);
      }
    }

    if (it != nullptr && it < end) {
      if (it->fieldid == *fieldid) {
        (*oc)++;
      } else {
        *fieldid = it->fieldid;
        *oc = 0;
      }

      if (len != nullptr) {
        auto flen = flength(it);

        if (*len < flen) {
          *len = flen;
          FERROR(FNOSPACE, "");
          return -1;
        } else if (value != nullptr) {
          std::copy_n(fvalue(it), flen, value);
        }
        *len = flen;
      }
      return 1;
    }
    return 0;
  }

  long len(FLDID32 fieldid, FLDOCC32 oc) {
    auto field = where(fieldid, oc);
    if (field == nullptr || field->fieldid != fieldid) {
      FERROR(FNOTPRES, "");
      return -1;
    }

    return flength(field);
  }

  int fprint(FILE *iop, int indent = 0) {
    iterate([&](auto it, auto) {
      auto name = Fname32(it->fieldid);
      for (int i = 0; i < indent; i++) {
        fputc('\t', iop);
      }
      if (name != nullptr) {
        fprintf(iop, "%s\t", name);
      } else {
        fprintf(iop, "(FLDID32(%d))\t", it->fieldid);
      }

      auto type = Fldtype32(it->fieldid);
      if (type == FLD_SHORT) {
        fprintf(iop, "%d", reinterpret_cast<field8b *>(it)->s);
      } else if (type == FLD_CHAR) {
        print_bytes(iop, &reinterpret_cast<field8b *>(it)->c, 1);
      } else if (type == FLD_FLOAT) {
        fprintf(iop, "%f", reinterpret_cast<field8b *>(it)->f);
      } else if (type == FLD_LONG) {
        fprintf(iop, "%ld", reinterpret_cast<field16b *>(it)->l);
      } else if (type == FLD_DOUBLE) {
        fprintf(iop, "%f", reinterpret_cast<field16b *>(it)->d);
      } else if (type == FLD_STRING) {
        auto field = reinterpret_cast<fieldn *>(it);
        print_bytes(iop, field->data, field->flen - 1);
      } else if (type == FLD_CARRAY) {
        auto field = reinterpret_cast<fieldn *>(it);
        print_bytes(iop, field->data, field->flen);
      } else if (type == FLD_FML32) {
        auto field = reinterpret_cast<fieldn *>(it);
        auto fbfr = reinterpret_cast<FBFR32 *>(field->data);
        fputc('\n', iop);
        fbfr->fprint(iop, indent + 1);
      }

      fputc('\n', iop);
      return 0;
    });

    if (indent == 0) {
      fputc('\n', iop);
    }
    return 0;
  }

  int del(FLDID32 fieldid, FLDOCC32 oc) {
    auto field = where(fieldid, oc);

    while (field == nullptr || field->fieldid != fieldid) {
      FERROR(FNOTPRES, "");
      return -1;
    }

    auto next = next_(field);
    erase(field, next);
    return 0;
  }

  int delall(FLDID32 fieldid) {
    auto field = where(fieldid, 0);

    while (field == nullptr || field->fieldid != fieldid) {
      FERROR(FNOTPRES, "");
      return -1;
    }

    auto next = field;
    while (next != nullptr && next->fieldid == fieldid) {
      next = next_(next);
    }
    erase(field, next);
    return 0;
  }

  int get(FLDID32 fieldid, FLDOCC32 oc, char *loc, FLDLEN32 *maxlen) {
    auto field = where(fieldid, oc);

    while (field == nullptr || field->fieldid != fieldid) {
      FERROR(FNOTPRES, "");
      return -1;
    }

    auto len = flength(field);
    if (maxlen != nullptr) {
      if (*maxlen < len) {
        *maxlen = len;
        FERROR(FNOSPACE, "");
        return -1;
      }
      *maxlen = len;
    }
    if (loc != nullptr) {
      if (Fldtype32(fieldid) != FLD_FML32) {
        std::copy_n(fvalue(field), len, loc);
      } else {
        std::copy_n(fvalue(field) + offsetof(Fbfr32, len_), len - sizeof(size_),
                    loc + offsetof(Fbfr32, len_));
      }
    }
    return 0;
  }

  int xdelete(FLDID32 *fieldid) {
    FLDID32 *badfld;
    if (sort(fieldid, &badfld) == -1) {
      return -1;
    }

    iterate([&](auto it, auto) {
      if (std::find(fieldid, badfld, it->fieldid) != badfld) {
        delall(it->fieldid);
        return 1;
      }
      return 0;
    });
    return 0;
  }

  int projcpy(FBFR32 *src, FLDID32 *fieldid) {
    FLDID32 *badfld;
    if (sort(fieldid, &badfld) == -1) {
      return -1;
    }
    init(size());

    return src->iterate([&](auto it, auto) {
      if (std::find(fieldid, badfld, it->fieldid) != badfld) {
        return chg(it->fieldid, occur(it->fieldid), src->fvalue(it),
                   src->flength(it));
      }
      return 0;
    });
  }

  int proj(FLDID32 *fieldid) {
    FLDID32 *badfld;
    if (sort(fieldid, &badfld) == -1) {
      return -1;
    }

    return iterate([&](auto it, auto) {
      if (std::find(fieldid, badfld, it->fieldid) == badfld) {
        delall(it->fieldid);
        return 1;
      }
      return 0;
    });
  }

  int update(FBFR32 *src) {
    return src->iterate([&](auto it, auto oc) {
      return chg(it->fieldid, oc, src->fvalue(it), src->flength(it));
    });
  }

  int ojoin(FBFR32 *src) {
    return src->iterate([&](auto it, auto oc) {
      if (pres(it->fieldid, oc)) {
        return chg(it->fieldid, oc, src->fvalue(it), src->flength(it));
      }
      return 0;
    });
  }

  int iterate(std::function<int(fieldhead *, FLDOCC32)> func) {
    auto it = reinterpret_cast<fieldhead *>(data_);
    auto end = reinterpret_cast<fieldhead *>(data_ + len_);

    FLDOCC32 oc = 0;
    FLDID32 prev = BADFLDID;

    while (it != nullptr && it < end) {
      if (it->fieldid != prev) {
        oc = 0;
        prev = it->fieldid;
      } else {
        oc++;
      }

      int r = func(it, oc);
      if (r == -1) {
        return -1;
      } else if (r == 1) {
        end = reinterpret_cast<fieldhead *>(data_ + len_);
      } else {
        it = next_(it);
      }
    }
    return 0;
  }

  int join(FBFR32 *src) {
    iterate([&](auto it, auto oc) {
      if (src->pres(it->fieldid, oc)) {
        return 0;
      } else {
        del(it->fieldid, oc);
        return 1;
      }
    });

    return src->iterate([&](auto it, auto oc) {
      if (pres(it->fieldid, oc)) {
        return chg(it->fieldid, oc, src->fvalue(it), src->flength(it));
      }
      return 0;
    });
  }

  int concat(FBFR32 *src) {
    if (unused() < src->used()) {
      FERROR(FNOSPACE, "");
      return -1;
    }

    return src->iterate([&](auto it, auto) {
      return chg(it->fieldid, occur(it->fieldid), src->fvalue(it),
                 src->flength(it));
    });
  }

  char *getalloc(FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *extralen) {
    auto field = where(fieldid, oc);

    while (field == nullptr || field->fieldid != fieldid) {
      FERROR(FNOTPRES, "");
      return nullptr;
    }

    auto len = flength(field);
    auto size = len;
    if (extralen != nullptr) {
      size += *extralen;
      *extralen = len;
    }
    char *loc = (char *)malloc(size);
    std::copy_n(fvalue(field), len, loc);
    return loc;
  }

 private:
  void print_bytes(FILE *iop, const char *s, int len) {
    for (int i = 0; i < len; i++, s++) {
      if (isprint(*s)) {
        fputc(*s, iop);
      } else {
        fprintf(iop, "\\%02x", *s);
      }
    }
  }

  void erase(fieldhead *from, fieldhead *to) {
    if (to == nullptr) {
      to = reinterpret_cast<fieldhead *>(data_ + len_);
    }

    auto diff = reinterpret_cast<char *>(to) - reinterpret_cast<char *>(from);
    memmove(from, to, (data_ + len_) - reinterpret_cast<char *>(to));
    shift(Fldtype32(from->fieldid), -diff);
  }

  int offset_for(int type) {
    switch (type) {
      case FLD_SHORT:
        return min_offset_;
      case FLD_LONG:
        return long_;
      case FLD_CHAR:
        return char_;
      case FLD_FLOAT:
        return float_;
      case FLD_DOUBLE:
        return double_;
      case FLD_STRING:
        return string_;
      case FLD_CARRAY:
        return carray_;
      case FLD_FML32:
        return fml32_;
      default:
        return max_offset_;
    }
  }

  uint32_t first_byte(int type) {
    int off = offset_for(type);
    if (off == min_offset_) {
      return 0;
    }
    return offsets_[off];
  }

  uint32_t last_byte(int type) {
    int off = offset_for(type) + 1;
    if (off == max_offset_) {
      return len_;
    }
    return offsets_[off];
  }

  void shift(int type, ssize_t delta) {
    len_ += delta;
    for (int off = offset_for(type) + 1; off < max_offset_; off++) {
      offsets_[off] += delta;
    }
  }

  uint32_t size_;
  uint32_t len_;
  enum type_offset {
    min_offset_ = -1,
    long_ = 0,
    char_,
    float_,
    double_,
    string_,
    carray_,
    fml32_,
    max_offset_
  };
  uint32_t offsets_[max_offset_];
  char data_[] __attribute__((aligned(8)));

  size_t min_size() const { return offsetof(Fbfr32, data_); }

  size_t value_len(int type, char *data, FLDLEN32 flen) {
    switch (type) {
      case FLD_SHORT:
        return sizeof(short);
      case FLD_CHAR:
        return sizeof(char);
      case FLD_FLOAT:
        return sizeof(float);
      case FLD_LONG:
        return sizeof(long);
      case FLD_DOUBLE:
        return sizeof(double);
      case FLD_STRING:
        return strlen(data) + 1;
      case FLD_CARRAY:
        return flen;
      case FLD_FML32:
        return Fused32(reinterpret_cast<FBFR32 *>(data));
      default:
        __builtin_unreachable();
    }
  }

  fieldhead *next_(fieldhead *head) {
    fieldhead *next;
    auto klass = fldclass(head->fieldid);
    if (klass == FIELD8) {
      auto field = reinterpret_cast<field8b *>(head);
      next = ++field;
    } else if (klass == FIELD16) {
      auto field = reinterpret_cast<field16b *>(head);
      next = ++field;
    } else if (klass == FIELDN) {
      auto field = reinterpret_cast<fieldn *>(head);
      next = reinterpret_cast<fieldn *>(field->data + field->size());
    } else {
      __builtin_unreachable();
    }

    auto end = reinterpret_cast<fieldhead *>(data_ + len_);
    if (next < end) {
      return next;
    }
    return nullptr;
  }

  size_t flength(fieldhead *field) {
    switch (Fldtype32(field->fieldid)) {
      case FLD_SHORT:
        return sizeof(short);
      case FLD_CHAR:
        return sizeof(char);
      case FLD_FLOAT:
        return sizeof(float);
      case FLD_LONG:
        return sizeof(long);
      case FLD_DOUBLE:
        return sizeof(double);
      case FLD_STRING:
      case FLD_CARRAY:
      case FLD_FML32:
        return reinterpret_cast<fieldn *>(field)->flen;
      default:
        __builtin_unreachable();
    }
  }

  char *fvalue(fieldhead *field) {
    auto klass = fldclass(field->fieldid);
    if (klass == FIELD8) {
      return reinterpret_cast<field8b *>(field)->data;
    } else if (klass == FIELD16) {
      return reinterpret_cast<field16b *>(field)->data;
    } else if (klass == FIELDN) {
      return reinterpret_cast<fieldn *>(field)->data;
    }
    __builtin_unreachable();
  }

  fieldhead *end(int type) {
    return reinterpret_cast<fieldhead *>(data_ + last_byte(type));
  }

  fieldhead *where(FLDID32 fieldid, FLDOCC32 oc) {
    int type = Fldtype32(fieldid);
    auto klass = fldclass(fieldid);

    if (klass == FIELD8) {
      return where<field8b>(fieldid, oc, first_byte(type), last_byte(type));
    } else if (klass == FIELD16) {
      return where<field16b>(fieldid, oc, first_byte(type), last_byte(type));
    } else if (klass == FIELDN) {
      return wheren(fieldid, oc, first_byte(type), last_byte(type));
    } else {
      __builtin_unreachable();
    }
  }

  template <class T>
  fieldhead *where(FLDID32 fieldid, FLDOCC32 oc, uint32_t from, uint32_t to) {
    auto begin = reinterpret_cast<T *>(data_ + from);
    auto end = reinterpret_cast<T *>(data_ + to);
    if (begin == end) {
      return nullptr;
    }
    FLDOCC32 count = 0;
    auto it = std::lower_bound(begin, end, T(fieldid));
    while (it < end) {
      if (it->fieldid > fieldid) {
        break;
      } else if (it->fieldid == fieldid) {
        if (count == oc) {
          break;
        }
        count++;
      }
      ++it;
    }
    if (it == end) {
      return nullptr;
    }
    return it;
  }

  fieldhead *wheren(FLDID32 fieldid, FLDOCC32 oc, uint32_t from, uint32_t to) {
    auto it = reinterpret_cast<fieldn *>(data_ + from);
    auto end = reinterpret_cast<fieldn *>(data_ + to);
    if (it == end) {
      return nullptr;
    }
    FLDOCC32 count = 0;
    while (it < end) {
      if (it->fieldid > fieldid) {
        break;
      } else if (it->fieldid == fieldid) {
        if (count == oc) {
          break;
        }
        count++;
      }
      it = reinterpret_cast<fieldn *>(it->data + it->size());
    }

    if (it == end) {
      return nullptr;
    }
    return it;
  }

  static int sort(FLDID32 *fieldid, FLDID32 **badfld = nullptr) {
    auto end = fieldid;
    while (*end != BADFLDID) {
      end++;
    }
    std::sort(fieldid, end);
    if (badfld != nullptr) {
      *badfld = end;
    }
    return 0;
  }

  enum storage_class { FIELD8, FIELD16, FIELDN };

  static constexpr storage_class fldclass(FLDID32 fieldid) {
    switch (Fbfr32fields::Fldtype32(fieldid)) {
      case FLD_SHORT:
      case FLD_CHAR:
      case FLD_FLOAT:
        return FIELD8;
      case FLD_LONG:
      case FLD_DOUBLE:
        return FIELD16;
      case FLD_STRING:
      case FLD_CARRAY:
      case FLD_FML32:
        return FIELDN;
      default:
        __builtin_unreachable();
    }
  }
};

// Hooks for buffer type below

void fml32init(void *mem, size_t size) {
  reinterpret_cast<Fbfr32 *>(mem)->init(size);
}

void fml32reinit(void *mem, size_t size) {
  reinterpret_cast<Fbfr32 *>(mem)->reinit(size);
}

void fml32finit(void *mem) { reinterpret_cast<Fbfr32 *>(mem)->finit(); }

size_t fml32used(void *mem) { return reinterpret_cast<Fbfr32 *>(mem)->used(); }
