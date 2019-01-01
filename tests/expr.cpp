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

#include <catch.hpp>

#include <fml32.h>
#include <xatmi.h>
#include <cstdlib>
#include <cstring>

#define DECONST(x) const_cast<char *>(x)

TEST_CASE("invalid boolean expr", "[fml32]") {
  REQUIRE((Fboolco32(nullptr) == nullptr && Ferror32 == FEINVAL));
  REQUIRE(
      (Fboolco32(DECONST("NAME == 'John")) == nullptr && Ferror32 == FSYNTAX));
  REQUIRE((Fboolco32(DECONST("foobar == 'John'")) == nullptr &&
           Ferror32 == FBADNAME));
  REQUIRE((Fboolco32(DECONST("FFML32 == 'John'")) == nullptr &&
           Ferror32 == FEBADOP));
  REQUIRE(
      (Fboolco32(DECONST("NAME <> 'John'")) == nullptr && Ferror32 == FSYNTAX));
  REQUIRE((Fboolco32(DECONST("NAME[-1] == 'John'")) == nullptr &&
           Ferror32 == FSYNTAX));
  REQUIRE((Fboolco32(DECONST("NAME %% 1")) == nullptr && Ferror32 == FSYNTAX));
  REQUIRE(
      (Fboolco32(DECONST("1 %% 'John'")) == nullptr && Ferror32 == FSYNTAX));
}

TEST_CASE("boolean expression conversions", "[fml32]") {
  char *tree;
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto VALUE = Fldid32(DECONST("VALUE"));

  REQUIRE(Fchg32(fbfr, VALUE, 0, DECONST("000001"), 0) != -1);

  // if one side is string field it is converted to number
  REQUIRE((tree = Fboolco32(DECONST("VALUE == '000001'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("VALUE == 1"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("VALUE == 1.0"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  // if one side is string constant second is converted to string
  REQUIRE((tree = Fboolco32(DECONST("'001' == 1"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression occurances", "[fml32]") {
  char *tree;
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));
  auto VALUE = Fldid32(DECONST("VALUE"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("name1"), 0) != -1);
  REQUIRE(Fchg32(fbfr, VALUE, 0, DECONST("000001"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 1, DECONST("name2"), 0) != -1);
  REQUIRE(Fchg32(fbfr, VALUE, 2, DECONST("000002"), 0) != -1);

  REQUIRE((tree = Fboolco32(DECONST("NAME == 'name1'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[0] == 'name1'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[1] == 'name1'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);
  REQUIRE((tree = Fboolco32(DECONST("NAME[1] != 'name1'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[1] == 'name2'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression '?' subscript", "[fml32]") {
  char *tree;
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));
  auto SALARY = Fldid32(DECONST("SALARY"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("name1"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 1, DECONST("name2"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 2, DECONST("name3"), 0) != -1);

  float salary;
  salary = 100.0;
  REQUIRE(Fchg32(fbfr, SALARY, 0, reinterpret_cast<char *>(&salary), 0) != -1);
  salary = 200.0;
  REQUIRE(Fchg32(fbfr, SALARY, 0, reinterpret_cast<char *>(&salary), 0) != -1);
  salary = 300.0;
  REQUIRE(Fchg32(fbfr, SALARY, 0, reinterpret_cast<char *>(&salary), 0) != -1);

  REQUIRE((tree = Fboolco32(DECONST("NAME[?] == 'name1'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[?] == '\\6Ea\\6de\\31'"))) !=
          nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[?] == '\\6E\\61\\6d\\65\\31'"))) !=
          nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[?] == 'name3'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[?] == 'name4'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);
  REQUIRE((tree = Fboolco32(DECONST("NAME[?] != 'name4'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("SALARY[?] < 100"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("SALARY[?] > 400"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("SALARY[?] > 222"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("SALARY[?] >= 300"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression regex", "[fml32]") {
  char *tree;
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("foo"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 1, DECONST("bar"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 2, DECONST("foobar"), 0) != -1);

  REQUIRE((tree = Fboolco32(DECONST("NAME[0] %% 'foo'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[0] %% 'fooBAR'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[1] %% '.a.'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[0] !% '.*r$'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME[?] %% '......'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression regex full string matching", "[fml32]") {
  char *tree;
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("1234567890"), 0) != -1);

  REQUIRE((tree = Fboolco32(DECONST("NAME %% '[0-1].*'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME !% '[0-1].*'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME %% '[2-3].*'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("NAME !% '[2-3].*'"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression unary", "[fml32]") {
  auto fbfr = Falloc32(100, 100);
  char *tree;

  REQUIRE((tree = Fboolco32(DECONST("!(!NAME)"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("!!NAME"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  auto NAME = Fldid32(DECONST("NAME"));
  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("13"), 0) != -1);

  REQUIRE((tree = Fboolco32(DECONST("!(!NAME)"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);

  Ffree32(fbfr);
}

TEST_CASE("boolean expression Ffloatev32", "[fml32]") {
  auto fbfr = Falloc32(100, 100);
  char *tree;

  REQUIRE((tree = Fboolco32(DECONST("1+2"))) != nullptr);
  REQUIRE(Ffloatev32(fbfr, tree) == 3);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("1+-2"))) != nullptr);
  REQUIRE(Ffloatev32(fbfr, tree) == -1);
  free(tree);

  REQUIRE((tree = Fboolco32(DECONST("-1+-2"))) != nullptr);
  REQUIRE(Ffloatev32(fbfr, tree) == -3);
  free(tree);

  auto SALARY = Fldid32(DECONST("SALARY"));

  float salary;
  salary = 100.0;
  REQUIRE(Fchg32(fbfr, SALARY, 0, reinterpret_cast<char *>(&salary), 0) != -1);

  REQUIRE((tree = Fboolco32(DECONST("1+SALARY"))) != nullptr);
  REQUIRE(Ffloatev32(fbfr, tree) == 101);
  free(tree);

  Ffree32(fbfr);
}

TEST_CASE("boolean eval", "[fml32]") {
  auto fbfr = Falloc32(100, 100);
  char *tree;

  REQUIRE((tree = Fboolco32(DECONST("1 == 1"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 1);
  free(tree);
  REQUIRE((tree = Fboolco32(DECONST("1 != 1"))) != nullptr);
  REQUIRE(Fboolev32(fbfr, tree) == 0);
  free(tree);

  Ffree32(fbfr);
}
