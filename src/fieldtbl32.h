#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>
#include <map>
#include <memory>
#include <vector>

#include "basic_parser.h"

static const std::map<std::string, int> field_types = {
    {"char", FLD_CHAR},     {"short", FLD_SHORT},   {"float", FLD_FLOAT},
    {"long", FLD_LONG},     {"double", FLD_DOUBLE}, {"string", FLD_STRING},
    {"carray", FLD_CARRAY}, {"fml32", FLD_FML32}};

class field_table_parser : public basic_parser {
 public:
  field_table_parser(std::istream &f) : basic_parser(f), base_(0) {}

  void parse() {
    while (parse_line())
      ;
    if (sym_ != EOF) {
      throw basic_parser_error("unrecognized input", row_, "");
    }
  }

  struct field {
    std::string name;
    FLDID32 fieldid;
    std::string comment;
  };

  // todo C++17 std::variant
  struct entry {
    // comment
    std::unique_ptr<std::string> c;
    // raw
    std::unique_ptr<std::string> r;
    // field
    std::unique_ptr<field> f;
  };

  const std::vector<field> &fields() const { return fields_; }
  const std::vector<entry> &entries() const { return entries_; }

 private:
  bool parse_line() {
    if (accept('#')) {
      std::string s;
      rest_of_line(&s);
      entries_.push_back({std::make_unique<std::string>(s), nullptr, nullptr});
    } else if (accept('$')) {
      std::string raw;
      rest_of_line(&raw);
      entries_.push_back(
          {nullptr, std::make_unique<std::string>(raw), nullptr});
    } else if (accept('*')) {
      std::string base, num;
      bool read = name(&base) && delim() && unsigned_number(&num);
      if (!read || base != "base") {
        throw basic_parser_error("unrecognized input", row_, "*base\toffset");
      }
      base_ = std::stol(num);

      delim();
      if (accept('#')) {
        rest_of_line();
      }
    } else {
      std::string fname;
      if (field_name(&fname)) {
        std::string num, type, c;

        // short, long, float, double, char, string, carray
        auto read = delim() && unsigned_number(&num) && delim() &&
                    name(&type) && rest_of_line(&c);
        // flags are optional as well
        if (!read) {
          throw basic_parser_error("unrecognized input", row_,
                                   "name\trel-number\ttype\t-\tcomment");
        }
        auto t = field_types.find(type);
        if (t == field_types.end()) {
          throw basic_parser_error("unsupported field type", row_,
                                   "found " + type +
                                       ", expected: char, short, float, long, "
                                       "double, string, carray, fml32");
        }

        fields_.push_back(
            {fname, Fmkfldid32(t->second, base_ + std::stol(num)), c});
        entries_.push_back(
            {nullptr, nullptr, std::make_unique<field>(fields_.back())});
      } else {
        accept([](int c) { return ::isspace(c) && c != '\n'; });
      }
    }

    return accept('\n');
  }

  bool delim() {
    if (accept([](int c) { return c == ' ' || c == '\t'; })) {
      while (accept([](int c) { return c == ' ' || c == '\t'; }))
        ;
      return true;
    }
    return false;
  }

  long base_;
  std::vector<field> fields_;
  std::vector<entry> entries_;
};
