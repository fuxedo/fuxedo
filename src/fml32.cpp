// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include <shared_mutex>

#include <fml32.h>
#include <regex.h>
#include "fieldtbl32.h"

#include <iostream>

unsigned int crc32b(unsigned char *data, size_t len) {
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

std::vector<std::string> split(const std::string &s, const std::string &delim) {
  std::vector<std::string> tokens;
  std::string token;

  for (auto const c : s) {
    if (delim.find(c) == std::string::npos) {
      token += c;
    } else if (!token.empty()) {
      tokens.push_back(token);
      token.clear();
    }
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  return tokens;
}

struct Fbfr32fields {
  std::shared_timed_mutex mutex;
  std::map<FLDID32, std::string> id_to_name;
  std::map<std::string, FLDID32> name_to_id;
  std::atomic<bool> id_to_name_loaded;
  std::atomic<bool> name_to_id_loaded;

  Fbfr32fields() : id_to_name_loaded(false), name_to_id_loaded(false) {}

  static constexpr int Fldtype32(FLDID32 fieldid) { return fieldid >> 24; }
  static constexpr long Fldno32(FLDID32 fieldid) { return fieldid & 0xffffff; }

  char *name(FLDID32 fieldid) {
    if (!id_to_name_loaded) {
      load_fieldtbls32();
    }
    std::shared_lock<std::shared_timed_mutex> lock(mutex);
    auto it = id_to_name.find(fieldid);
    if (it != id_to_name.end()) {
      return const_cast<char *>(it->second.c_str());
    }
    Ferror32 = FBADFLD;
    return nullptr;
  }

  FLDID32 fldid(const char *name) {
    if (!name_to_id_loaded) {
      load_fieldtbls32();
    }
    std::shared_lock<std::shared_timed_mutex> lock(mutex);
    auto it = name_to_id.find(name);
    if (it != name_to_id.end()) {
      return it->second;
    }
    Ferror32 = FBADNAME;
    return BADFLDID;
  }

  void idnm_unload() {
    std::unique_lock<std::shared_timed_mutex> lock(mutex);
    id_to_name.clear();
    id_to_name_loaded = false;
  }

  void nmid_unload() {
    std::unique_lock<std::shared_timed_mutex> lock(mutex);
    name_to_id.clear();
    name_to_id_loaded = false;
  }

 private:
  bool read_fld32_file(const std::string &fname) {
    std::ifstream fields(fname);
    if (!fields) {
      return false;
    }

    field_table_parser p(fields);
    p.parse();
    for (auto field : p.fields()) {
      id_to_name.insert(make_pair(field.fieldid, field.name));
      name_to_id.insert(make_pair(field.name, field.fieldid));
    }

    return true;
  }

  void load_fieldtbls32() {
    auto fieldtbls32 = getenv("FIELDTBLS32");
    auto fldtbldir32 = getenv("FLDTBLDIR32");

    if (fieldtbls32 == nullptr || fldtbldir32 == nullptr) {
      return;
    }

    auto files = split(fieldtbls32, ",");
    auto dirs = split(fldtbldir32, ":");

    std::unique_lock<std::shared_timed_mutex> lock(mutex);

    id_to_name.clear();
    name_to_id.clear();

    id_to_name_loaded = true;
    name_to_id_loaded = true;

    for (auto &fname : files) {
      for (auto &dname : dirs) {
        if (read_fld32_file(dname + "/" + fname)) {
          break;
        }
      }
    }
  }
};

static Fbfr32fields Fbfr32fields_;
static thread_local char conv_val_[32] __attribute__((aligned(8)));

int Fldtype32(FLDID32 fieldid) { return Fbfr32fields::Fldtype32(fieldid); }

long Fldno32(FLDID32 fieldid) { return Fbfr32fields::Fldno32(fieldid); }

char *Fname32(FLDID32 fieldid) { return Fbfr32fields_.name(fieldid); }

FLDID32 Fldid32(FUXCONST char *name) { return Fbfr32fields_.fldid(name); }

void Fidnm_unload32() { return Fbfr32fields_.idnm_unload(); }

void Fnmid_unload32() { return Fbfr32fields_.nmid_unload(); }

////////////////////////////////////////////////////////////////////////////

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
  };
};
static_assert(sizeof(field16b) == 16, "Large fixed fields must be 16 bytes");

struct fieldn : fieldhead {
  FLDLEN32 flen;
  char data[];

  FLDLEN32 size() { return size(flen); }
  static constexpr FLDLEN32 size(FLDLEN32 ilen) {
    if (ilen & 3) {
      ilen += 4 - (ilen & 3);
    }
    return ilen;
  }
};
static_assert(sizeof(fieldn) == 8, "Variable field header must be 8 bytes");

class Fbfr32 {
  Fbfr32() = delete;
  Fbfr32(const Fbfr32 &) = delete;
  Fbfr32 &operator=(const Fbfr32 &) = delete;

 public:
  static constexpr long needed(FLDOCC32 F, FLDLEN32 V) {
    return F * (sizeof(FLDID32) + sizeof(uint32_t) + V) + sizeof(Fbfr32);
  }

  int init(FLDLEN32 buflen) {
    if (buflen < min_size()) {
      Ferror32 = FNOSPACE;
      return -1;
    }
    size_ = buflen - min_size();
    len_ = 0;

    offset_long_ = 0;
    offset_char_ = 0;
    offset_float_ = 0;
    offset_double_ = 0;
    offset_string_ = 0;
    offset_carray_ = 0;
    offset_fml32_ = 0;
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
      Ferror32 = FNOSPACE;
      return -1;
    }
    len_ = src->len_;
    offset_long_ = src->offset_long_;
    offset_char_ = src->offset_char_;
    offset_double_ = src->offset_double_;
    offset_float_ = src->offset_float_;
    offset_string_ = src->offset_string_;
    offset_carray_ = src->offset_carray_;
    offset_fml32_ = src->offset_fml32_;
    std::copy_n(src->data_, src->len_, data_);
    return 0;
  }

  int write(FILE *iop) {
    // Writes out everything starting with len
    auto n = used() - sizeof(size_);
    if (fwrite(&len_, 1, n, iop) != n) {
      Ferror32 = FEUNIX;
      return -1;
    }
    return 0;
  }

  int read(FILE *iop) {
    uint32_t len;
    if (fread(&len, 1, sizeof(len), iop) != sizeof(len)) {
      Ferror32 = FEUNIX;
      return -1;
    }
    if (len > size_) {
      Ferror32 = FNOSPACE;
      return -1;
    }

    len_ = len;
    auto n = used() - sizeof(size_) - sizeof(len_);
    if (fread(reinterpret_cast<char *>(&len_) + sizeof(len_), 1, n, iop) != n) {
      Ferror32 = FEUNIX;
      return -1;
    }
    return 0;
  }

  int chg(FLDID32 fieldid, FLDOCC32 oc, const char *value, FLDLEN32 flen) {
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
    }

    auto field = where(fieldid, oc);
    if (field == nullptr) {
      field = end(type);
    } else if (field->fieldid == fieldid) {
      if (klass == FIELD8 || klass == FIELD16) {
        need = 0;
      } else if (klass == FIELDN) {
        need -= sizeof(fieldn) + reinterpret_cast<fieldn *>(field)->size();
      }
    }

    if (need != 0) {
      if (len_ + need > size_) {
        Ferror32 = FNOSPACE;
        return -1;
      }

      auto ptr = reinterpret_cast<char *>(field);
      // std::copy(ptr, data_ + len_, ptr + need);
      // copy does not work for overlaps?
      memmove(ptr + need, ptr, (data_ + len_) - ptr);
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
        Ferror32 = FEINVAL;
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
    Ferror32 = FNOTPRES;
    return -1;
  }

  char *find(FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *flen) {
    auto field = where(fieldid, oc);
    if (field == nullptr || field->fieldid != fieldid) {
      Ferror32 = FNOTPRES;
      return nullptr;
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
      Ferror32 = FEINVAL;
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
        if (value != nullptr && *len >= flen) {
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
      Ferror32 = FNOTPRES;
      return -1;
    }

    return flength(field);
  }

  int fprint(FILE *iop, int indent = 0) {
    auto it = reinterpret_cast<fieldhead *>(data_);
    auto end = reinterpret_cast<fieldhead *>(data_ + len_);

    while (it != nullptr && it < end) {
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

      it = next_(it);
    }

    if (indent == 0) {
      fputc('\n', iop);
    }
    return 0;
  }

  int del(FLDID32 fieldid, FLDOCC32 oc) {
    auto field = where(fieldid, oc);

    while (field == nullptr || field->fieldid != fieldid) {
      Ferror32 = FNOTPRES;
      return -1;
    }

    auto next = next_(field);
    erase(field, next);
    return 0;
  }

  int delall(FLDID32 fieldid) {
    auto field = where(fieldid, 0);

    while (field == nullptr || field->fieldid != fieldid) {
      Ferror32 = FNOTPRES;
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
      Ferror32 = FNOTPRES;
      return -1;
    }

    auto len = flength(field);
    if (maxlen != nullptr) {
      if (*maxlen < len) {
        *maxlen = len;
        Ferror32 = FNOSPACE;
        return -1;
      }
      *maxlen = len;
    }
    if (loc != nullptr) {
      std::copy_n(fvalue(field), len, loc);
    }
    return 0;
  }

  int xdelete(FLDID32 *fieldid) {
    FLDID32 *badfld;
    if (sort(fieldid, &badfld) == -1) {
      return -1;
    }

    auto it = reinterpret_cast<fieldhead *>(data_);
    auto end = reinterpret_cast<fieldhead *>(data_ + len_);

    while (it != nullptr && it < end) {
      auto f = std::find(fieldid, badfld, it->fieldid);
      if (f == badfld) {
        it = next_(it);
      } else {
        delall(it->fieldid);
        end = reinterpret_cast<fieldhead *>(data_ + len_);
      }
    }
    return 0;
  }

  int projcpy(FBFR32 *src, FLDID32 *fieldid) {
    FLDID32 *badfld;
    if (sort(fieldid, &badfld) == -1) {
      return -1;
    }
    init(size());

    auto it = reinterpret_cast<fieldhead *>(src->data_);
    auto end = reinterpret_cast<fieldhead *>(src->data_ + src->len_);

    while (it != nullptr && it < end) {
      auto f = std::find(fieldid, badfld, it->fieldid);
      if (f != badfld) {
        auto oc = occur(it->fieldid);

        if (chg(it->fieldid, oc, src->fvalue(it), src->flength(it)) == -1) {
          return -1;
        }
      }
      it = src->next_(it);
    }
    return 0;
  }

  int proj(FLDID32 *fieldid) {
    FLDID32 *badfld;
    if (sort(fieldid, &badfld) == -1) {
      return -1;
    }

    auto it = reinterpret_cast<fieldhead *>(data_);
    auto end = reinterpret_cast<fieldhead *>(data_ + len_);

    while (it != nullptr && it < end) {
      auto f = std::find(fieldid, badfld, it->fieldid);
      if (f == badfld) {
        delall(it->fieldid);
        end = reinterpret_cast<fieldhead *>(data_ + len_);
      } else {
        it = next_(it);
      }
    }
    return 0;
  }

  int update(FBFR32 *src) {
    auto it = reinterpret_cast<fieldhead *>(src->data_);
    auto end = reinterpret_cast<fieldhead *>(src->data_ + src->len_);

    FLDOCC32 oc = 0;
    FLDID32 prev = BADFLDID;
    while (it != nullptr && it < end) {
      if (it->fieldid != prev) {
        oc = 0;
        prev = it->fieldid;
      } else {
        oc++;
      }
      if (chg(it->fieldid, oc, src->fvalue(it), src->flength(it)) == -1) {
        return -1;
      }

      it = src->next_(it);
    }
    return 0;
  }

  int join(FBFR32 *src) {
    {
      auto it = reinterpret_cast<fieldhead *>(data_);
      auto end = reinterpret_cast<fieldhead *>(data_ + len_);

      while (it != nullptr && it < end) {
        if (src->pres(it->fieldid, 0)) {
          it = next_(it);
        } else {
          delall(it->fieldid);
          end = reinterpret_cast<fieldhead *>(data_ + len_);
        }
      }
    }

    {
      auto it = reinterpret_cast<fieldhead *>(src->data_);
      auto end = reinterpret_cast<fieldhead *>(src->data_ + src->len_);

      FLDOCC32 oc = 0;
      FLDID32 prev = BADFLDID;
      while (it != nullptr && it < end) {
        if (it->fieldid != prev) {
          oc = 0;
          prev = it->fieldid;
        } else {
          oc++;
        }
        if (pres(it->fieldid, 0)) {
          if (chg(it->fieldid, oc, src->fvalue(it), src->flength(it)) == -1) {
            return -1;
          }
        }
        it = src->next_(it);
      }
    }
    return 0;
  }

  int concat(FBFR32 *src) {
    if (unused() < src->used()) {
      Ferror32 = FNOSPACE;
      return -1;
    }

    auto it = reinterpret_cast<fieldhead *>(src->data_);
    auto end = reinterpret_cast<fieldhead *>(src->data_ + src->len_);

    while (it != nullptr && it < end) {
      auto oc = occur(it->fieldid);

      if (chg(it->fieldid, oc, src->fvalue(it), src->flength(it)) == -1) {
        return -1;
      }
      it = src->next_(it);
    }
    return 0;
  }

  int ojoin(FBFR32 *src) {
    auto it = reinterpret_cast<fieldhead *>(src->data_);
    auto end = reinterpret_cast<fieldhead *>(src->data_ + src->len_);

    FLDOCC32 oc = 0;
    FLDID32 prev = BADFLDID;
    while (it != nullptr && it < end) {
      if (it->fieldid != prev) {
        oc = 0;
        prev = it->fieldid;
      } else {
        oc++;
      }
      if (pres(it->fieldid, oc)) {
        if (chg(it->fieldid, oc, src->fvalue(it), src->flength(it)) == -1) {
          return -1;
        }
      }
      it = src->next_(it);
    }
    return 0;
  }

  char *getalloc(FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *extralen) {
    auto field = where(fieldid, oc);

    while (field == nullptr || field->fieldid != fieldid) {
      Ferror32 = FNOTPRES;
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

  void shift(int type, ssize_t delta) {
    switch (type) {
      case FLD_SHORT:
        offset_long_ += delta;
      case FLD_LONG:
        offset_char_ += delta;
      case FLD_CHAR:
        offset_float_ += delta;
      case FLD_FLOAT:
        offset_double_ += delta;
      case FLD_DOUBLE:
        offset_string_ += delta;
      case FLD_STRING:
        offset_carray_ += delta;
      case FLD_CARRAY:
        offset_fml32_ += delta;
      case FLD_FML32:
        len_ += delta;
      default:
        break;
    }
  }

  uint32_t size_;
  uint32_t len_;
  uint32_t offset_long_;
  uint32_t offset_char_;
  uint32_t offset_float_;
  uint32_t offset_double_;
  uint32_t offset_string_;
  uint32_t offset_carray_;
  uint32_t offset_fml32_;
  char data_[] __attribute__((aligned(8)));

  size_t min_size() const { return offsetof(Fbfr32, data_); }

  size_t value_len(int type, const char *data, FLDLEN32 flen) {
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
        return Fused32(reinterpret_cast<const FBFR32 *>(data));
      default:
        return 0;
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
        return 0;
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
  }

  fieldhead *end(int type) {
    if (type == FLD_SHORT) {
      return reinterpret_cast<fieldhead *>(data_ + offset_long_);
    } else if (type == FLD_LONG) {
      return reinterpret_cast<fieldhead *>(data_ + offset_char_);
    } else if (type == FLD_CHAR) {
      return reinterpret_cast<fieldhead *>(data_ + offset_float_);
    } else if (type == FLD_FLOAT) {
      return reinterpret_cast<fieldhead *>(data_ + offset_double_);
    } else if (type == FLD_DOUBLE) {
      return reinterpret_cast<fieldhead *>(data_ + offset_string_);
    } else if (type == FLD_STRING) {
      return reinterpret_cast<fieldhead *>(data_ + offset_carray_);
    } else if (type == FLD_CARRAY) {
      return reinterpret_cast<fieldhead *>(data_ + offset_fml32_);
    } else if (type == FLD_FML32) {
      return reinterpret_cast<fieldhead *>(data_ + len_);
    }
  }

  fieldhead *where(FLDID32 fieldid, FLDOCC32 oc) {
    switch (Fldtype32(fieldid)) {
      case FLD_SHORT:
        return where<field8b>(fieldid, oc, 0, offset_long_);
      case FLD_LONG:
        return where<field16b>(fieldid, oc, offset_long_, offset_char_);
      case FLD_CHAR:
        return where<field8b>(fieldid, oc, offset_char_, offset_float_);
      case FLD_FLOAT:
        return where<field8b>(fieldid, oc, offset_float_, offset_double_);
      case FLD_DOUBLE:
        return where<field16b>(fieldid, oc, offset_double_, offset_string_);
      case FLD_STRING:
        return wheren(fieldid, oc, offset_string_, offset_carray_);
      case FLD_CARRAY:
        return wheren(fieldid, oc, offset_carray_, offset_fml32_);
      case FLD_FML32:
        return wheren(fieldid, oc, offset_fml32_, len_);
      default:
        return nullptr;
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

  enum storage_class { INVCLASS, FIELD8, FIELD16, FIELDN };

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
        return INVCLASS;
    }
  }
};

int *_Ferror32_tls() {
  thread_local int Ferror32_ = 0;
  return &Ferror32_;
}

FUXCONST char *Fstrerror32(int err) {
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

FLDID32 Fmkfldid32(int type, FLDID32 num) {
  if (type != FLD_SHORT && type != FLD_LONG && type != FLD_CHAR &&
      type != FLD_FLOAT && type != FLD_DOUBLE && type != FLD_STRING &&
      type != FLD_CARRAY && type != FLD_FML32) {
    Ferror32 = FTYPERR;
    return -1;
  }
  if (num < 1) {
    Ferror32 = FBADFLD;
    return -1;
  }

  return (type << 24) | num;
}
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
      Ferror32 = FNOTFLD;                                    \
      return err;                                            \
    }                                                        \
    if ((reinterpret_cast<intptr_t>(fbfr) & (8 - 1)) != 0) { \
      Ferror32 = FNOTFLD;                                    \
      return err;                                            \
    }                                                        \
  } while (false);

int Finit32(FBFR32 *fbfr, FLDLEN32 buflen) {
  FBFR32_CHECK(-1, fbfr);
  if (buflen < Fneeded32(0, 0)) {
    Ferror32 = FNOSPACE;
  }
  return fbfr->init(buflen);
}

FBFR32 *Frealloc32(FBFR32 *fbfr, FLDOCC32 F, FLDLEN32 V) {
  FBFR32_CHECK(nullptr, fbfr);

  long buflen = Fneeded32(F, V);
  if (buflen < Fused32(fbfr)) {
    Ferror32 = FMALLOC;
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

void fml32init(void *mem, size_t size) {
  reinterpret_cast<Fbfr32 *>(mem)->init(size);
}

void fml32reinit(void *mem, size_t size) {
  reinterpret_cast<Fbfr32 *>(mem)->reinit(size);
}

void fml32finit(void *mem) { reinterpret_cast<Fbfr32 *>(mem)->finit(); }

long Fsizeof32(FUXCONST FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->size();
}

long Fused32(FUXCONST FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->used();
}

long Funused32(FUXCONST FBFR32 *fbfr) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->unused();
}

long Fidxused32(FUXCONST FBFR32 *fbfr) {
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

int Fchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FUXCONST char *value,
           FLDLEN32 len) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->chg(fieldid, oc, value, len);
}

int Fadd32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len) {
  FBFR32_CHECK(-1, fbfr);
  auto oc = fbfr->occur(fieldid);
  return fbfr->chg(fieldid, oc, value, len);
}

char *Ffind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len) {
  FBFR32_CHECK(nullptr, fbfr);
  return fbfr->find(fieldid, oc, len);
}
int Fpres32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->pres(fieldid, oc);
}

FLDOCC32 Foccur32(FBFR32 *fbfr, FLDID32 fieldid) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->occur(fieldid);
}

long Flen32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  FBFR32_CHECK(-1, fbfr);
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
  return fbfr->del(fieldid, oc);
}

int Fdelall32(FBFR32 *fbfr, FLDID32 fieldid) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->delall(fieldid);
}

int Fget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc,
           FLDLEN32 *maxlen) {
  FBFR32_CHECK(-1, fbfr);
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
    Ferror32 = FEBADOP;                                      \
    return nullptr;                                          \
  }

#define TYPCVTS                                              \
  if (totype == FLD_SHORT) {                                 \
    COPY_N(static_cast<short>(atol(from.c_str())));          \
  } else if (totype == FLD_CHAR) {                           \
    COPY_N(static_cast<char>(atol(from.c_str())));           \
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
    Ferror32 = FEBADOP;                                      \
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
    Ferror32 = FEBADOP;
    return nullptr;
  }
  return &toval[0];
}

FLDOCC32 Ffindocc32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len) {
  FBFR32_CHECK(-1, fbfr);
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
  FLDLEN32 rlen = -1;
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
  if (rlen != -1 && *len > rlen) {
    Ferror32 = FNOSPACE;
    return -1;
  }
  std::copy_n(res, *len, buf);
  return 0;
}

char *Ffinds32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
  return CFfind32(fbfr, fieldid, oc, nullptr, FLD_STRING);
}

int Fgets32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *buf) {
  return CFget32(fbfr, fieldid, oc, buf, nullptr, FLD_STRING);
}

int Fwrite32(FBFR32 *fbfr, FILE *iop) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->write(iop);
}

int Fread32(FBFR32 *fbfr, FILE *iop) {
  FBFR32_CHECK(-1, fbfr);
  return fbfr->read(iop);
}
