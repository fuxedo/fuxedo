#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>
#include <map>
#include <memory>
#include <vector>

#include "basic_parser.h"

static const std::string supported_flags = "CL";
class view_parser : public basic_parser {
 public:
  view_parser(std::istream &f) : basic_parser(f), done_(false) {}

  void parse() {
    while (parse_line())
      ;
    if (sym_ != EOF) {
      throw basic_parser_error("unrecognized input", row_, "");
    }
    if (!done_) {
      throw basic_parser_error("invalid/incomplete file", row_, "");
    }
  }

  struct field {
    std::string type, cname, fbname;
    long count;
    std::string flag, size, nullv;
  };

  // todo C++17 std::variant
  struct entry {
    // raw
    std::unique_ptr<std::string> r;
    // field
    std::unique_ptr<field> f;
    // view
    std::unique_ptr<std::string> view;
    std::unique_ptr<std::string> end;
  };

  const std::vector<field> &fields() const { return fields_; }
  const std::vector<entry> &entries() const { return entries_; }

 private:
  bool parse_line() {
    if (accept('#')) {
      rest_of_line();
    } else if (accept('$')) {
      std::string raw;
      rest_of_line(&raw);
      entries_.push_back(
          {std::make_unique<std::string>(raw), nullptr, nullptr, nullptr});
    } else {
      std::string first;
      if (field_name(&first)) {
        if (first == "END") {
          if (name_.empty()) {
            throw basic_parser_error("No view definition", row_,
                                     "VIEW\tVIEWNAME\nEND");
          }
          done_ = true;
      entries_.push_back(
          {nullptr, nullptr, nullptr, std::make_unique<std::string>("end")});
        } else if (first == "VIEW") {
          if (!name_.empty()) {
            throw basic_parser_error("Duplicate view name", row_,
                                     "VIEW\tVIEWNAME");
          }
          if (!delim() || !field_name(&name_)) {
            throw basic_parser_error("View name expected", row_,
                                     "VIEW\tVIEWNAME");
          }
        entries_.push_back(
          {nullptr, nullptr, std::make_unique<std::string>(name_), nullptr});
        } else {
          std::string type = first;

          std::string cname, fbname, count, flag, size, nullv;

          // short, long, float, double, char, string, carray
          auto read = delim() && name(&cname) && delim() &&
                      opt_field(&fbname) && delim() &&
                      unsigned_number(&count) && delim() && opt_field(&flag) &&
                      delim() && opt_number(&size) && delim() &&
                      rest_of_line(&nullv);
          if (!read) {
            throw basic_parser_error(
                "unrecognized input", row_,
                "type\ncname\tfbname\tcount\tflag\tsize\tnull");
          }

          if (size.size() > 0) {
            if (type != "string" && type != "carray" && type != "dec_t") {
              throw basic_parser_error("Size supported only for string, carray and dec_t types", row_, "");
            }
          }

          for (auto c : flag) {
            if (supported_flags.find(c) == std::string::npos) {
              throw basic_parser_error("Invalid flags", row_, "Only " + supported_flags + " supported");
            }
          }

          fields_.push_back({type, cname, fbname, std::stol(count), flag, size, nullv});
          entries_.push_back(
              {nullptr, std::make_unique<field>(fields_.back()), nullptr, nullptr});
        }
      } else {
        accept([](int c) { return ::isspace(c) && c != '\n'; });
      }
    }

    return accept('\n');
  }

  bool opt_field(std::string *s = nullptr) { return accept('-') || name(s); }
  bool opt_number(std::string *s = nullptr) {
    return accept('-') || unsigned_number(s);
  }

  bool null_value(std::string *s) {
    s->clear();
    if (accept('-')) {
      return true;
    }
    if (accept('"')) {
      while (accept([](int c) { return c != '"'; }, s))
        ;
      return accept('"');
    } else {
      return rest_of_line(s);
    }
  }

  bool delim() {
    if (accept([](int c) { return c == ' ' || c == '\t'; })) {
      while (accept([](int c) { return c == ' ' || c == '\t'; }))
        ;
      return true;
    }
    return false;
  }

  bool done_;
  std::string name_;
  std::vector<field> fields_;
  std::vector<entry> entries_;
};
