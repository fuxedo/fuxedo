#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fml32.h>
#include "basic_parser.h"

struct Fbfr32;

namespace fux::view {

struct member {
  std::string cname;
  std::string fbname;  // field table name, empty if unmapped
  FLDID32 fldid = 0;
  int type = FLD_LONG;  // FLD_SHORT, FLD_LONG, FLD_CHAR, FLD_FLOAT,
                        // FLD_DOUBLE, FLD_STRING, FLD_CARRAY
  int32_t count = 1;
  int flags = F_STOF | F_FTOS;  // two-way mapping unless restricted
  int32_t elsize = 0;
  std::vector<char> nullval;
  std::vector<char> fill;      // NULL fill: nullval, P-padded, for sinit/Fvnull32
  std::vector<char> fill_nop;  // same without the P propagation, for Fvstof32
  bool null_none = false;  // NONE: no value is considered NULL
  int32_t off = -1;
  int32_t acm_off = -1;
  int32_t alm_off = -1;
};

struct viewdef {
  std::string name;
  std::vector<std::string> code;  // $ lines copied into the header
  std::vector<member> members;
  int32_t vsize = 0;
};

// Source viewfile parser. Throws basic_parser_error with FVFSYNTAX-worthy
// messages; the caller maps to Ferror32.
class viewfile_parser : public basic_parser {
 public:
  viewfile_parser(std::istream &f) : basic_parser(f) {}

  std::vector<viewdef> parse();

 private:
  bool parse_line(viewdef *&cur);

  bool opt_field(std::string *s = nullptr) { return accept('-') || name(s); }
  bool opt_number(std::string *s = nullptr) {
    return accept('-') || unsigned_number(s);
  }
  bool opt_flags(std::string *s = nullptr) {
    return accept('-') || flag_list(s);
  }
  // comma-separated flag list: C, F, L, N, P, S
  bool flag_list(std::string *s) {
    if (accept([](int c) { return ::isalpha(c); }, s)) {
      while (accept([](int c) { return ::isalpha(c) || c == ','; }, s))
        ;
      return true;
    }
    return false;
  }

  std::vector<viewdef> views_;
};

// Turns parsed views into compiled ones: validates types and flags,
// resolves fbname through Fldid32 (unless no_fml), computes NULL values,
// detects duplicate cname/fldid. Appends human readable problems to `errors`
// and `warnings`; returns false if any errors were found.
bool resolve(std::vector<viewdef> &views, bool no_fml,
             std::vector<std::string> &errors, std::vector<std::string> &warnings);

// Computes member offsets and the structure size following the C ABI.
void layout(viewdef &view);

// Binary viewfile (.V) codec.
void write_file(std::ostream &out, const std::vector<viewdef> &views);
std::vector<viewdef> read_file(std::istream &in);

// Runtime registry backed by VIEWFILES32/VIEWDIR32 with an LRU cache.
// Returns nullptr and sets Ferror32 (FBADVIEW/FVFOPEN/FVFSYNTAX/FEUNIX) when
// the view cannot be found. The returned object is a mutable per-process
// copy: Fvopt32 changes live in it until eviction or refresh().
std::shared_ptr<viewdef> get(const std::string &name);
void refresh();

// Conversion engine operating on an attached Fbfr32.
int to_struct(Fbfr32 *fbfr, char *cstruct, viewdef &view);
int from_struct(Fbfr32 *fbfr, char *cstruct, int mode, viewdef &view);
bool is_null(const member &m, const char *el);
void sinit(char *cstruct, viewdef &view);
void selinit(char *cstruct, member &m);
const member *find_member(const viewdef &view, const std::string &cname);

}  // namespace fux::view
