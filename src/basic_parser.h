#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdio>
#include <cstring>
#include <functional>
#include <istream>
#include <stdexcept>

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

  bool field_name(std::string *s = nullptr) {
    if (accept(::isalpha, s)) {
      while (accept(::isalnum, s) || accept('_', s))
        ;
      return true;
    }
    return false;
  }

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

    __builtin_unreachable();
  }
};
