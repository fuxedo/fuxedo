#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <memory>

#include <fml32.h>
#include "basic_parser.h"
#include "fux.h"

class extreader : public basic_parser {
 public:
  extreader(std::istream &f) : basic_parser(f) {}
  extreader(FILE *f) : basic_parser(f) {}

  bool parse() {
    if (eof()) {
      return false;
    }
    while (parse_line(buf, 0))
      ;
    return true;
  }
  FBFR32 *get() { return buf.get(); }

  // ud tool protocol: field lines accumulate into fml, a line with just
  // "n" starts a new buffer, a blank line sends the current buffer and
  // returns true. At EOF the pending buffer is dropped.
  bool parse_ud(fux::fml32ptr &fml) {
    while (sym_ != EOF) {
      if (accept('\n')) {
        return true;
      }
      if (sym_ == 'n') {
        std::string tok;
        accept([](int c) { return c != '\t' && c != '\n' && c != EOF; }, &tok);
        if (tok == "n" && (sym_ == '\n' || sym_ == EOF)) {
          accept('\n');
          Finit32(fml.ptr(), Fsizeof32(fml.ptr()));
          continue;
        }
        parse_field(fml, 0, tok);
        continue;
      }
      parse_field(fml, 0, "");
    }
    return false;
  }

 private:
  fux::fml32ptr buf;
  fux::fml32ptr parse_buf(int indent) {
    auto fml = fux::fml32ptr();
    while (parse_line(fml, indent))
      ;
    return fml;
  }

  bool parse_line(fux::fml32ptr &fml, int indent) {
    if (sym_ == EOF) {
      return false;
    }
    if (accept('\n')) {
      // empty line terminates the buffer
      return false;
    }
    return parse_field(fml, indent, "");
  }

  bool parse_field(fux::fml32ptr &fml, int indent,
                   const std::string &first_token) {
    for (int i = 0; i < indent; i++) {
      if (!accept('\t')) {
        throw basic_parser_error(
            "incomplete input", row_, col_,
            "expected tab, found '" + std::string(1, sym_) + "'");
      }
    }

    std::string f, fname, fvalue;

    if (!first_token.empty()) {
      fname = first_token;
    } else {
      flag(&f);
      if (!field_name(&fname)) {
        throw basic_parser_error(
            "field name not found", row_, col_,
            "expected field name, found '" + std::string(1, sym_) + "'");
      }
    }
    if (!accept('\t')) {
      throw basic_parser_error(
          "incomplete input", row_, col_,
          "expected tab, found '" + std::string(1, sym_) + "'");
    }

    value(&fvalue);

    if (!accept('\n')) {
      throw basic_parser_error(
          "incomplete input", row_, col_,
          "expected newline '\\n', found '" + std::string(1, sym_) + "'");
    }

    auto fieldid = Fldid32(const_cast<char *>(fname.c_str()));
    if (fieldid == BADFLDID) {
    }

    if (Fldtype32(fieldid) == FLD_FML32) {
      auto nested = parse_buf(indent + 1);
      fml.mutate([&](FBFR32 *fbfr) {
        return Fadd32(fbfr, fieldid, reinterpret_cast<char *>(nested.get()), 0);
      });
    } else {
      fml.mutate([&](FBFR32 *fbfr) {
        return CFadd32(fbfr, fieldid, const_cast<char *>(fvalue.c_str()), 0,
                       FLD_STRING);
      });
    }

    return true;
  }

  bool flag(std::string *s = nullptr) {
    if (accept([](int c) { return strchr("+-=", c) != nullptr; }, s)) {
      return true;
    }
    return false;
  }

  bool value(std::string *s = nullptr) {
    while (true) {
      if (accept('\\')) {
        hex(s);
      } else if (accept([](int c) { return c != '\n'; }, s)) {
      } else {
        break;
      }
    }
    // No value is fine as well
    return true;
  }
};
