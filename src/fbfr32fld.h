#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <map>
#include <shared_mutex>

#include <fml32.h>
#include "extreader.h"
#include "fieldtbl32.h"
#include "misc.h"

#include <iostream>

#define FERROR32(err, fmt, args...)                                        \
  fux::fml32::set_Ferror32(err, "%s() in %s:%d: " fmt, __func__, __FILE__, \
                           __LINE__, ##args)

namespace fux {
namespace fml32 {
void set_Ferror32(int err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void reset_Ferror32();
}  // namespace fml32
}  // namespace fux

struct Fbfr32fields {
  std::shared_timed_mutex mutex;
  std::map<FLDID32, std::string> id_to_name;
  std::map<std::string, FLDID32, std::less<>> name_to_id;
  std::atomic<bool> id_to_name_loaded;
  std::atomic<bool> name_to_id_loaded;

  Fbfr32fields() : id_to_name_loaded(false), name_to_id_loaded(false) {}

  static constexpr bool valid_fldtype32(int type) {
    if (type != FLD_SHORT && type != FLD_LONG && type != FLD_CHAR &&
        type != FLD_FLOAT && type != FLD_DOUBLE && type != FLD_STRING &&
        type != FLD_CARRAY && type != FLD_FML32) {
      return false;
    }
    return true;
  }

  static constexpr FLDID32 Fmkfldid32(int type, FLDID32 num) {
    if (!valid_fldtype32(type)) {
      Ferror32 = FTYPERR;
      return -1;
    }
    if (num < 1) {
      Ferror32 = FBADFLD;
      return -1;
    }

    return (type << 24) | num;
  }

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

    auto files = fux::split(fieldtbls32, ",");
    auto dirs = fux::split(fldtbldir32, ":");

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
