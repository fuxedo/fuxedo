#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdio>
#include <cstring>
#include <functional>
#include <istream>
#include <map>
#include <stdexcept>
#include <string>

class basic_parser_error : public std::runtime_error {
 public:
  basic_parser_error(const char *what, int row, std::string extra)
      : std::runtime_error(what), row(row), col(-1), extra(std::move(extra)) {}

  basic_parser_error(const char *what, int row, int col, std::string extra)
      : std::runtime_error(what), row(row), col(col), extra(std::move(extra)) {}

  const int row;
  const int col;
  const std::string extra;
};

class basic_parser {
 public:
  basic_parser(std::istream &f)
      : in_(f.rdbuf()),
        fin_(nullptr),
        count_(0),
        row_(1),
        col_(1),
        sym_(in_->sbumpc()) {}
  basic_parser(FILE *f)
      : in_(nullptr), fin_(f), count_(0), row_(1), col_(1), sym_(fgetc(f)) {}

 protected:
  bool eof() { return sym_ == EOF; }
  bool accept(std::function<bool(int)> f, std::string *s = nullptr) {
    if (sym_ == EOF || !f(sym_)) {
      return false;
    }
    if (s != nullptr) {
      s->push_back(sym_);
    }
    next();
    return true;
  }
  bool accept(int ch, std::string *s = nullptr) {
    return accept([ch](int c) { return c == ch; }, s);
  }

  bool rest_of_line(std::string *s = nullptr) {
    while (accept([](int c) { return c != '\n'; }, s))
      ;
    return true;
  }

  bool hex(std::string *s = nullptr) {
    if (strchr("0123456789abcdefABCDEF", sym_) != nullptr) {
      char high = sym_;
      next();
      if (strchr("0123456789abcdefABCDEF", sym_) != nullptr) {
        char low = sym_;
        next();
        if (s != nullptr) {
          s->push_back(base16(high) << 4 | base16(low));
        }
        return true;
      }
      throw basic_parser_error(
          "Invalid hex character", row_, col_,
          "'" + std::string(1, sym_) + "' is not a valid hex character");
    }
    return false;
  }

  bool unsigned_number(std::string *s = nullptr) {
    if (accept(::isdigit, s)) {
      while (accept(::isdigit, s))
        ;
      accept('.', s);
      while (accept(::isdigit, s))
        ;
      return true;
    }
    return false;
  }

  bool field_name(std::string *s = nullptr) { return name(s); }

  bool name(std::string *s = nullptr) {
    if (accept(::isalpha, s)) {
      while (accept(::isalnum, s) || accept('_', s))
        ;
      return true;
    }
    return false;
  }

  bool space() {
    if (accept(::isspace)) {
      while (accept(::isspace))
        ;
      return true;
    }
    return false;
  }

  // Higher-level combinators shared by the config-file grammars.

  // run of whitespace excluding the newline
  bool blank() {
    if (accept([](int c) { return ::isspace(c) && c != '\n'; })) {
      while (accept([](int c) { return ::isspace(c) && c != '\n'; }))
        ;
      return true;
    }
    return false;
  }

  // spaces and tabs only; some grammars treat '\r' as content
  bool delim() {
    if (accept([](int c) { return c == ' ' || c == '\t'; })) {
      while (accept([](int c) { return c == ' ' || c == '\t'; }))
        ;
      return true;
    }
    return false;
  }

  // a quoted ("...") or bare token; bare tokens stop at whitespace and
  // '#' and, unless allow_eq, at '='
  bool token(std::string *s = nullptr, bool allow_eq = true) {
    if (accept('"')) {
      while (accept([](int c) { return c != '"' && c != EOF; }, s))
        ;
      if (!accept('"')) {
        throw basic_parser_error("closing quote expected", row_, "");
      }
      return true;
    }
    if (accept([](int c) { return !::isspace(c) && c != '#' && c != EOF; },
               s)) {
      while (accept([=](int c) {
               return !::isspace(c) && c != '#' && c != EOF &&
                      (allow_eq || c != '=');
             }, s))
        ;
      return true;
    }
    return false;
  }

  // blank line or a #-comment line
  bool skip_line() {
    blank();
    if (accept('#')) {
      while (accept([](int c) { return c != '\n' && c != EOF; }))
        ;
    }
    return accept('\n');
  }

  // *SECTION header line
  bool section(std::string *name) {
    if (!accept('*')) {
      return false;
    }
    if (!this->name(name)) {
      throw basic_parser_error("section name expected", row_, "*SECTION");
    }
    if (!skip_line()) {
      throw basic_parser_error("newline expected", row_, "*SECTION");
    }
    return true;
  }

  // KEY=VALUE parameters until the entry ends; entries continue on the
  // next line when it is indented or the current one ends in '\'. With
  // bare_flags a parameter without '=' takes the next token as its
  // value; with first_wins a repeated key keeps its first value.
  bool kv_list(std::map<std::string, std::string> *out, bool bare_flags,
               bool first_wins = false) {
    while (true) {
      blank();
      if (accept('\n')) {
        if (!blank()) {
          return true;
        }
      } else if (accept('\\')) {
        if (!accept('\n')) {
          throw basic_parser_error("newline expected after \\", row_, "");
        }
        blank();
      }
      std::string key;
      if (!token(&key, false)) {
        if (bare_flags) {
          return true;
        }
        throw basic_parser_error("key=value expected", row_, "KEY");
      }
      blank();
      std::string value;
      if (accept('=')) {
        blank();
        if (!token(&value)) {
          throw basic_parser_error("value expected", row_, "VALUE");
        }
      } else if (bare_flags) {
        if (!token(&value)) {
          throw basic_parser_error("value expected", row_, "VALUE");
        }
      } else {
        throw basic_parser_error("key=value expected", row_, "=");
      }
      if (first_wins) {
        out->insert(std::make_pair(key, value));
      } else {
        (*out)[key] = value;
      }
    }
  }

  void next() {
    if (fin_ != nullptr) {
      sym_ = fgetc(fin_);
    } else {
      sym_ = in_->sgetc();
      in_->snextc();
    }
    ++count_;
    if (sym_ == '\n') {
      ++row_;
      col_ = 1;
    } else if (::isprint(sym_)) {
      ++col_;
    }
  }

  std::streambuf *in_;
  FILE *fin_;
  int count_;
  int row_;
  int col_;
  int sym_;

 private:
  static constexpr int base16(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';

    __builtin_unreachable();  // LCOV_EXCL_LINE
  }
};
