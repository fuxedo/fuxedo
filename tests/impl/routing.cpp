// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <stdexcept>

#include "../../src/routing.h"

using fux::routing::buftype_matches;
using fux::routing::field_kind;
using fux::routing::match;
using fux::routing::parse_ranges;

TEST_CASE("parse_ranges basic") {
  auto r = parse_ranges("0-10:G1,11-20:G2");
  REQUIRE(r.size() == 2);
  CHECK(r[0].lo == "0");
  CHECK(r[0].hi == "10");
  CHECK(r[0].groups == std::vector<std::string>{"G1"});
  CHECK(r[1].lo == "11");
  CHECK(r[1].hi == "20");
  CHECK(!r[0].catch_all);
  CHECK(!r[0].is_string);
}

TEST_CASE("parse_ranges MIN MAX single value") {
  auto r = parse_ranges("MIN-10:G1,11-MAX:G2,7:G3");
  REQUIRE(r.size() == 3);
  CHECK(r[0].lo == "MIN");
  CHECK(r[1].hi == "MAX");
  CHECK(r[2].hi.empty());
  CHECK(r[2].lo == "7");
}

TEST_CASE("parse_ranges catch-all and group lists") {
  auto r = parse_ranges("*:*,0-10:G1;G2");
  REQUIRE(r.size() == 2);
  CHECK(r[0].catch_all);
  CHECK(r[0].groups == std::vector<std::string>{"*"});
  CHECK(r[1].groups == std::vector<std::string>({"G1", "G2"}));
}

TEST_CASE("parse_ranges quoted strings") {
  auto r = parse_ranges("'ABCD':G1,*:G2");
  REQUIRE(r.size() == 2);
  CHECK(r[0].is_string);
  CHECK(r[0].lo == "ABCD");
}

TEST_CASE("parse_ranges rejects malformed") {
  CHECK_THROWS(parse_ranges(""));
  CHECK_THROWS(parse_ranges("0-10"));
  CHECK_THROWS(parse_ranges(":G1"));
  CHECK_THROWS(parse_ranges("0-10:"));
  CHECK_THROWS(parse_ranges("-5:G1"));
  CHECK_THROWS(parse_ranges("0-10:G1;"));
  CHECK_THROWS(parse_ranges("0-10:G1;;G2"));
}

TEST_CASE("match integer ranges first match wins") {
  auto r = parse_ranges("0-10:G1,11-20:G2");
  CHECK(match(r, "5", field_kind::integer)->groups[0] == "G1");
  CHECK(match(r, "15", field_kind::integer)->groups[0] == "G2");
  CHECK(match(r, "99", field_kind::integer) == nullptr);
  CHECK(match(r, "-5", field_kind::integer) == nullptr);
}

TEST_CASE("match MIN MAX bounds") {
  auto r = parse_ranges("MIN-0:G1,1-MAX:G2");
  CHECK(match(r, "-999999", field_kind::integer)->groups[0] == "G1");
  CHECK(match(r, "1", field_kind::integer)->groups[0] == "G2");
  CHECK(match(r, "999999", field_kind::integer)->groups[0] == "G2");
}

TEST_CASE("match catch-all") {
  auto r = parse_ranges("0-10:G1,*:G2");
  CHECK(match(r, "99", field_kind::integer)->groups[0] == "G2");
  CHECK(match(r, "anything", field_kind::string)->groups[0] == "G2");
}

TEST_CASE("match floating") {
  auto r = parse_ranges("0.5-2.5:G1");
  CHECK(match(r, "1.5", field_kind::floating)->groups[0] == "G1");
  CHECK(match(r, "3.5", field_kind::floating) == nullptr);
}

TEST_CASE("match string exact only") {
  auto r = parse_ranges("'ABCD':G1,*:G2");
  CHECK(match(r, "ABCD", field_kind::string)->groups[0] == "G1");
  CHECK(match(r, "ABC", field_kind::string)->groups[0] == "G2");
  // numeric bounds never match a string field
  auto n = parse_ranges("0-10:G1");
  CHECK(match(n, "5", field_kind::string) == nullptr);
}

TEST_CASE("match string ranges never match numeric fields") {
  auto r = parse_ranges("'ABCD':G1,0-10:G2");
  CHECK(match(r, "5", field_kind::integer)->groups[0] == "G2");
  CHECK(match(r, "15", field_kind::integer) == nullptr);
}

TEST_CASE("buftype_matches") {
  CHECK(buftype_matches("FML32", "FML32", ""));
  CHECK(!buftype_matches("FML", "FML32", ""));
  CHECK(buftype_matches("FML;FML32", "FML32", ""));
  CHECK(buftype_matches("VIEW:aud", "VIEW", "aud"));
  CHECK(!buftype_matches("VIEW:aud", "VIEW", "other"));
  CHECK(!buftype_matches("VIEW:aud", "VIEW", ""));
  CHECK(buftype_matches("VIEW32:v1;FML32", "FML32", ""));
}
