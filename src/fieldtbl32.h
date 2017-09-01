#pragma once
#include <fml32.h>
#include <map>
#include <vector>

#include "basic_parser.h"

static const std::map<std::string, int> field_types = {
    {"char", FLD_CHAR},     {"short", FLD_SHORT},   {"float", FLD_FLOAT},
    {"long", FLD_LONG},     {"double", FLD_DOUBLE}, {"string", FLD_STRING},
    {"carray", FLD_CARRAY}, {"fml32", FLD_FML32}};

class parser : public basic_parser {
 public:
  parser(std::istream &f) : basic_parser(f), base_(0) {}

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
  };

  const std::vector<field> &fields() const { return fields_; }

 private:
  bool parse_line() {
    if (accept('#')) {
      comment();
    } else if (accept('$')) {
      std::string raw;
      comment(&raw);
    } else if (accept('*')) {
      std::string base, num;
      bool read = name(&base) && delim() && unsigned_number(&num);
      if (!read || base != "base") {
        throw basic_parser_error("unrecognized input", row_, "*base\toffset");
      }
      base_ = std::stol(num);
    } else {
      std::string fname;
      if (field_name(&fname)) {
        std::string num, type;

        // short, long, float, double, char, string, carray
        auto read = delim() && unsigned_number(&num) && delim() &&
                    name(&type) && comment();
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
            {fname, Fmkfldid32(t->second, base_ + std::stol(num))});
      } else {
        accept([](int c) { return ::isspace(c) && c != '\n'; });
      }
    }

    return accept('\n');
  }

  bool name(std::string *s = nullptr) {
    // accept field name rules
    return field_name(s);
  }

  bool comment(std::string *s = nullptr) {
    while (accept([](int c) { return c != '\n'; }, s))
      ;
    return true;
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
};
