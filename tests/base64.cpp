// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <string>

// for base64 prototypes
#include "../src/misc.h"

static std::string b64encode(const std::string &s) {
  std::string result;
  result.resize(s.size() * 2 + 4);
  auto n = base64encode(s.data(), s.size(), &result[0], result.size());
  result.resize(n);
  return result;
}

static std::string b64decode(const std::string &s) {
  std::string result;
  result.resize(s.size() * 2 + 3);
  auto n = base64decode(s.data(), s.size(), &result[0], result.size());
  result.resize(n);
  return result;
}

TEST_CASE("encode strings", "[base64]") {
  REQUIRE(b64encode("fuxedo.io") == "ZnV4ZWRvLmlv");
  REQUIRE(b64encode("a") == "YQ==");
  REQUIRE(b64encode("ab") == "YWI=");
  REQUIRE(b64encode("abc") == "YWJj");
  REQUIRE(b64encode("") == "");
  REQUIRE(b64encode("abcdefghijklmnopqrstuvwxyz"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "0123456789!@#0^&*();:<>==. []{}") ==
          "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXpBQkNE"
          "RUZHSElKS0xNTk9QUVJTVFVWV1hZWjAxMjM0NT"
          "Y3ODkhQCMwXiYqKCk7Ojw+PT0uIFtde30=");
}

TEST_CASE("decode strings", "[base64]") {
  REQUIRE(b64decode("ZnV4ZWRvLmlv") == "fuxedo.io");
  REQUIRE(b64decode("YQ==") == "a");
  REQUIRE(b64decode("YWI=") == "ab");
  REQUIRE(b64decode("YWJj") == "abc");
  REQUIRE(b64decode("") == "");
  REQUIRE(b64decode("YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXpBQkNE"
                    "RUZHSElKS0xNTk9QUVJTVFVWV1hZWjAxMjM0NT"
                    "Y3ODkhQCMwXiYqKCk7Ojw+PT0uIFtde30=") ==
          "abcdefghijklmnopqrstuvwxyz"
          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
          "0123456789!@#0^&*();:<>==. []{}");

  REQUIRE_THROWS_AS(b64decode("1"), std::logic_error);
  REQUIRE_THROWS_AS(b64decode("12"), std::logic_error);
  REQUIRE_THROWS_AS(b64decode("123"), std::logic_error);
  REQUIRE_THROWS_AS(b64decode("===="), std::logic_error);
}
