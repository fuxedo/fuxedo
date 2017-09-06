#pragma once
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

#include <map>
#include <memory>
#include <vector>

#include <fml32.h>
#include "basic_parser.h"

class extreader : public basic_parser {
 public:
  extreader(std::istream &f) : basic_parser(f) {}
  extreader(FILE *f) : basic_parser(f) {}
  typedef std::unique_ptr<FBFR32, decltype(&Ffree32)> fml_raii;

  fml_raii parse() {
    try {
      return parse_buf();
    } catch (const basic_parser_error &e) {
      fprintf(stderr, "%s, %s\n", e.what(), e.extra.c_str());
      return fml_raii(nullptr, Ffree32);
    }
  }

 private:
  fml_raii parse_buf(int indent = 0) {
    auto fml = fml_raii(Falloc32(1, 1024), &Ffree32);

    while (parse_line(fml, indent))
      ;
    return fml;
  }

  bool parse_line(fml_raii &fml, int indent = 0) {
    if (accept('\n')) {
      // empty line terminates buffer
      return false;
    }

    for (int i = 0; i < indent; i++) {
      if (!accept('\t')) {
        throw basic_parser_error(
            "incomplete input", row_, col_,
            "expected tab, found '" + std::string(1, sym_) + "'");
      }
    }

    std::string f, fname, fvalue;
    flag(&f);

    if (!field_name(&fname)) {
      throw basic_parser_error(
          "field name not found", row_, col_,
          "expected field name, found '" + std::string(1, sym_) + "'");
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

    auto fieldid = Fldid32(fname.c_str());
    if (fieldid == BADFLDID) {
    }

    if (Fldtype32(fieldid) == FLD_FML32) {
      auto nested = parse_buf(indent + 1);
      auto ret =
          Fadd32(fml.get(), fieldid, reinterpret_cast<char *>(nested.get()), 0);
      if (ret == -1 && Ferror32 == FNOSPACE) {
        fml =
            fml_raii(Frealloc32(fml.get(), 1,
                                Fsizeof32(fml.get()) + Fsizeof32(nested.get())),
                     &Ffree32);
        ret = Fadd32(fml.get(), fieldid, reinterpret_cast<char *>(nested.get()),
                     0);
      }
    } else {
      auto ret = CFadd32(fml.get(), fieldid, const_cast<char *>(fvalue.c_str()),
                         0, FLD_STRING);
      if (ret == -1 && Ferror32 == FNOSPACE) {
        fml = fml_raii(Frealloc32(fml.get(), 1,
                                  Fsizeof32(fml.get()) + (fvalue.size() + 128)),
                       &Ffree32);
        ret = CFadd32(fml.get(), fieldid, const_cast<char *>(fvalue.c_str()), 0,
                      FLD_STRING);
      }
    }

    return true;
  }

  bool flag(std::string *s = nullptr) {
    if (accept([](int c) { return strchr("+-=", c) != nullptr; })) {
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

  bool comment(std::string *s = nullptr) {
    while (accept([](int c) { return c != '\n'; }, s))
      ;
    return true;
  }
};
