#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <string>
#include <vector>

namespace fux::routing {

enum class field_kind { string, integer, floating };

// One entry of a ROUTING RANGES list: an ordered range:groups pair.
// Bounds are kept as text; numeric comparison happens at match time
// when the routing field is numeric.
struct range {
  bool catch_all = false;  // bare '*'
  bool is_string = false;  // single-quoted bound, exact match
  std::string lo;          // "MIN", "MAX", number or string
  std::string hi;          // same; empty for a single value
  std::vector<std::string> groups;  // "G1", "G1;G2" list, or "*" (any)
};

// Splits a RANGES parameter value ("0-10:G1,11-MAX:G2,*:*") into
// ordered pairs. Throws std::runtime_error on a malformed entry.
std::vector<range> parse_ranges(const std::string &ranges);

// True when a BUFTYPE parameter value ("FML32", "VIEW:aud",
// "FML;FML32") covers the given buffer type and subtype.
bool buftype_matches(const std::string &spec, const char *type,
                     const char *subtype);

// First range matching the field value (numeric compare for integer
// and floating fields, exact string compare otherwise). Returns
// nullptr when nothing matches; the caller turns that into TPESYSTEM
// the way the platform does.
const range *match(const std::vector<range> &ranges, const std::string &value,
                   field_kind kind);

}  // namespace fux::routing
