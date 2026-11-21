// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "routing.h"

#include <cctype>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace fux::routing {

// "1000-5000:G1;G2" -> bounds "1000"-"5000", groups ["G1", "G2"]
// "'ABCD':G1"       -> string bound "ABCD"
// "*:*"             -> catch-all, any group
static range parse_one(const std::string &item) {
  range r;
  auto colon = item.find(':');
  if (colon == std::string::npos) {
    throw std::runtime_error("range entry without ':'");
  }
  auto spec = item.substr(0, colon);
  auto groups = item.substr(colon + 1);

  if (spec == "*") {
    r.catch_all = true;
  } else if (spec.size() >= 2 && spec.front() == '\'' && spec.back() == '\'') {
    r.is_string = true;
    r.lo = spec.substr(1, spec.size() - 2);
  } else {
    auto dash = spec.find('-');
    if (dash == std::string::npos) {
      r.lo = spec;
    } else {
      r.lo = spec.substr(0, dash);
      r.hi = spec.substr(dash + 1);
    }
    if (r.lo.empty() || (dash != std::string::npos && r.hi.empty()) ||
        r.lo == "*" || r.hi == "*") {
      throw std::runtime_error("invalid range bound");
    }
  }

  size_t begin = 0;
  while (begin <= groups.size()) {
    auto sep = groups.find(';', begin);
    auto g = groups.substr(begin, sep == std::string::npos
                                       ? std::string::npos
                                       : sep - begin);
    if (g.empty()) {
      throw std::runtime_error("empty group name");
    }
    r.groups.push_back(g);
    if (sep == std::string::npos) {
      break;
    }
    begin = sep + 1;
  }
  return r;
}

std::vector<range> parse_ranges(const std::string &ranges) {
  std::vector<range> out;
  size_t begin = 0;
  while (begin <= ranges.size()) {
    auto sep = ranges.find(',', begin);
    auto item = ranges.substr(
        begin, sep == std::string::npos ? std::string::npos : sep - begin);
    if (item.empty()) {
      throw std::runtime_error("empty range entry");
    }
    out.push_back(parse_one(item));
    if (sep == std::string::npos) {
      break;
    }
    begin = sep + 1;
  }
  return out;
}

bool buftype_matches(const std::string &spec, const char *type,
                     const char *subtype) {
  size_t begin = 0;
  while (begin <= spec.size()) {
    auto sep = spec.find(';', begin);
    auto item = spec.substr(
        begin, sep == std::string::npos ? std::string::npos : sep - begin);
    auto colon = item.find(':');
    auto t = item.substr(0, colon);
    if (t == type) {
      if (colon == std::string::npos) {
        return true;  // FML, FML32, XML: no subtype allowed
      }
      if (subtype != nullptr &&
          item.substr(colon + 1) == subtype) {
        return true;
      }
    }
    if (sep == std::string::npos) {
      break;
    }
    begin = sep + 1;
  }
  return false;
}

// MIN/MAX bound -> widest value of the field type
static long bound_long(const std::string &b) {
  if (b == "MIN") {
    return std::numeric_limits<long>::min();
  }
  if (b == "MAX") {
    return std::numeric_limits<long>::max();
  }
  return std::strtol(b.c_str(), nullptr, 10);
}

static double bound_double(const std::string &b) {
  if (b == "MIN") {
    return -std::numeric_limits<double>::max();
  }
  if (b == "MAX") {
    return std::numeric_limits<double>::max();
  }
  return std::strtod(b.c_str(), nullptr);
}

const range *match(const std::vector<range> &ranges, const std::string &value,
                   field_kind kind) {
  for (const auto &r : ranges) {
    if (r.catch_all) {
      return &r;
    }
    if (kind == field_kind::string) {
      if (r.is_string && r.lo == value) {
        return &r;
      }
      continue;
    }
    if (r.is_string) {
      continue;
    }
    if (kind == field_kind::integer) {
      long v = std::strtol(value.c_str(), nullptr, 10);
      long lo = bound_long(r.lo);
      long hi = r.hi.empty() ? lo : bound_long(r.hi);
      if (v >= lo && v <= hi) {
        return &r;
      }
    } else {
      double v = std::strtod(value.c_str(), nullptr);
      double lo = bound_double(r.lo);
      double hi = r.hi.empty() ? lo : bound_double(r.hi);
      if (v >= lo && v <= hi) {
        return &r;
      }
    }
  }
  return nullptr;
}

}  // namespace fux::routing
