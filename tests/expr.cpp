// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>

#include <fml32.h>
#include <xatmi.h>
#include <cstdlib>
#include <cstring>

#include "misc.h"

TEST_CASE("invalid boolean expr", "[fml32]") {
  REQUIRE((Fboolco32(DECONST("(NAME == 'John'")) == nullptr &&
           Ferror32 == FSYNTAX));
  REQUIRE((Fboolco32(DECONST("NAME ==")) == nullptr && Ferror32 == FSYNTAX));
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
  REQUIRE((Fboolco32(DECONST("1 >> 1")) == nullptr && Ferror32 == FSYNTAX));
}

static bool boolev(FBFR32 *fbfr, const std::string &expr) {
  char *tree;
  REQUIRE((tree = Fboolco32(DECONST(expr.c_str()))) != nullptr);
  int ret = Fboolev32(fbfr, tree);
  free(tree);
  return ret == 1;
}

static double numev(FBFR32 *fbfr, const std::string &expr) {
  char *tree;
  REQUIRE((tree = Fboolco32(DECONST(expr.c_str()))) != nullptr);
  auto ret = Ffloatev32(fbfr, tree);
  free(tree);
  return ret;
}

TEST_CASE("boolean expression conversions", "[fml32]") {
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto VALUE = Fldid32(DECONST("VALUE"));

  REQUIRE(Fchg32(fbfr, VALUE, 0, DECONST("000001"), 0) != -1);

  // if one side is string field it is converted to number
  REQUIRE(boolev(fbfr, "VALUE == '000001'"));
  REQUIRE(boolev(fbfr, "VALUE == 1"));
  REQUIRE(boolev(fbfr, "VALUE == 1.0"));

  // if one side is string constant second is converted to string
  REQUIRE(boolev(fbfr, "'1' == 1"));
  REQUIRE(boolev(fbfr, "'1.000000' == 1.0"));
  REQUIRE(!boolev(fbfr, "'001' == 1"));
  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression occurances", "[fml32]") {
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));
  auto VALUE = Fldid32(DECONST("VALUE"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("name1"), 0) != -1);
  REQUIRE(Fchg32(fbfr, VALUE, 0, DECONST("000001"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 1, DECONST("name2"), 0) != -1);
  REQUIRE(Fchg32(fbfr, VALUE, 2, DECONST("000002"), 0) != -1);

  REQUIRE(boolev(fbfr, "NAME == 'name1'"));

  REQUIRE(boolev(fbfr, "NAME[0] == 'name1'"));

  REQUIRE(boolev(fbfr, "NAME[0] == 'name1' && VALUE[0] == '000001'"));
  REQUIRE(boolev(fbfr, "NAME[0] == 'name1' || VALUE[0] == '000002'"));
  REQUIRE(boolev(fbfr, "NAME[0] == 'name2' || VALUE[0] == '000001'"));
  REQUIRE(!boolev(fbfr, "NAME[1] == 'name1'"));
  REQUIRE(boolev(fbfr, "NAME[1] != 'name1'"));
  REQUIRE(boolev(fbfr, "NAME[1] == 'name2'"));

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression '?' subscript", "[fml32]") {
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

  REQUIRE(boolev(fbfr, "NAME[?] == 'name1'"));

  REQUIRE(boolev(fbfr, "NAME[?] == '\\6Ea\\6de\\31'"));
  REQUIRE(boolev(fbfr, "NAME[?] == '\\6E\\61\\6d\\65\\31'"));

  REQUIRE(boolev(fbfr, "NAME[?] == 'name3'"));
  REQUIRE(!boolev(fbfr, "NAME[?] == 'name4'"));
  REQUIRE(boolev(fbfr, "NAME[?] != 'name4'"));

  REQUIRE(!boolev(fbfr, "SALARY[?] < 100"));
  REQUIRE(!boolev(fbfr, "SALARY[?] > 400"));
  REQUIRE(boolev(fbfr, "SALARY[?] > 222"));
  REQUIRE(boolev(fbfr, "SALARY[?] >= 300"));
  REQUIRE(boolev(fbfr, "SALARY[?] <= 300"));

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression regex", "[fml32]") {
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("foo"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 1, DECONST("bar"), 0) != -1);
  REQUIRE(Fchg32(fbfr, NAME, 2, DECONST("foobar"), 0) != -1);

  REQUIRE(boolev(fbfr, "NAME[0] %% 'foo'"));
  REQUIRE(!boolev(fbfr, "NAME[0] %% 'fooBAR'"));
  REQUIRE(boolev(fbfr, "NAME[1] %% '.a.'"));
  REQUIRE(boolev(fbfr, "NAME[0] !% '.*r$'"));
  REQUIRE(boolev(fbfr, "NAME[?] %% '......'"));

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression regex full string matching", "[fml32]") {
  auto fbfr = (FBFR32 *)tpalloc(DECONST("FML32"), DECONST("*"), 1024);

  auto NAME = Fldid32(DECONST("NAME"));

  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("1234567890"), 0) != -1);

  REQUIRE(boolev(fbfr, "NAME %% '[0-1].*'"));
  REQUIRE(!boolev(fbfr, "NAME !% '[0-1].*'"));
  REQUIRE(!boolev(fbfr, "NAME %% '[2-3].*'"));
  REQUIRE(boolev(fbfr, "NAME !% '[2-3].*'"));

  tpfree((char *)fbfr);
}

TEST_CASE("boolean expression unary", "[fml32]") {
  auto fbfr = Falloc32(100, 100);
  REQUIRE(!boolev(fbfr, "!(!NAME)"));
  REQUIRE(!boolev(fbfr, "!!NAME"));

  auto NAME = Fldid32(DECONST("NAME"));
  REQUIRE(Fchg32(fbfr, NAME, 0, DECONST("13"), 0) != -1);

  REQUIRE(boolev(fbfr, "!(!NAME)"));

  Ffree32(fbfr);
}

TEST_CASE("boolean expression Ffloatev32", "[fml32]") {
  auto fbfr = Falloc32(100, 100);

  REQUIRE(numev(fbfr, "1+2") == 3);
  REQUIRE(numev(fbfr, "1+~(~2)") == 3);
  REQUIRE(numev(fbfr, "+1+2") == 3);
  REQUIRE(numev(fbfr, "1+-2") == -1);
  REQUIRE(numev(fbfr, "-1+-2") == -3);
  REQUIRE(numev(fbfr, "1+2+4-6") == 1);
  REQUIRE(numev(fbfr, "1*2*3*4/8") == 3);
  REQUIRE(numev(fbfr, "5%2") == 1);

  REQUIRE(numev(fbfr, "1+SALARY") == 1);

  auto SALARY = Fldid32(DECONST("SALARY"));
  float salary = 100.0;
  REQUIRE(Fchg32(fbfr, SALARY, 0, reinterpret_cast<char *>(&salary), 0) != -1);

  REQUIRE(numev(fbfr, "1+SALARY") == 101);

  REQUIRE(numev(fbfr, "AGE + 1") == 1);

  auto AGE = Fldid32(DECONST("AGE"));
  long age = 18;
  REQUIRE(Fchg32(fbfr, AGE, 0, reinterpret_cast<char *>(&age), 0) != -1);

  REQUIRE(numev(fbfr, "AGE + 1") == 19);

  Ffree32(fbfr);
}

TEST_CASE("boolean eval", "[fml32]") {
  auto fbfr = Falloc32(100, 100);

  REQUIRE(boolev(fbfr, "1"));
  REQUIRE(boolev(fbfr, "1.2"));
  REQUIRE(!boolev(fbfr, "!1"));
  REQUIRE(!boolev(fbfr, "!1.2"));
  REQUIRE(boolev(fbfr, "1 == 1"));
  REQUIRE(!boolev(fbfr, "1 != 1"));
  REQUIRE(boolev(fbfr, "1 != -1"));
  REQUIRE(boolev(fbfr, "1 != ~1"));
  REQUIRE(boolev(fbfr, "1 == ~(~1)"));

  Ffree32(fbfr);
}

TEST_CASE("invalid inputs", "[fml32]") {
  auto fbfr = Falloc32(100, 100);
  char *tree = nullptr;

  REQUIRE(Fboolco32(nullptr) == nullptr);
  REQUIRE(Ferror32 == FEINVAL);

  REQUIRE(Fboolev32(nullptr, tree) == -1);
  REQUIRE(Ferror32 == FNOTFLD);
  REQUIRE(Fboolev32(fbfr, tree) == -1);
  REQUIRE(Ferror32 == FNOTFLD);
  REQUIRE(Ffloatev32(nullptr, tree) == -1);
  REQUIRE(Ferror32 == FNOTFLD);
  REQUIRE(Ffloatev32(fbfr, tree) == -1);
  REQUIRE(Ferror32 == FNOTFLD);

  Ffree32(fbfr);
}

static std::string compiled(const std::string &expr) {
  auto fbfr = Falloc32(100, 100);
  char *tree;

  REQUIRE((tree = Fboolco32(DECONST(expr.c_str()))) != nullptr);

  tempfile f(__LINE__);
  Fboolpr32(tree, f.f);
  fclose(f.f);
  free(tree);

  return read_file(f.name);
}

TEST_CASE("Fboolpr32", "[fml32]") {
  REQUIRE(compiled("1 == 1") == "( ( 1 ) == ( 1 ) ) \n");
  REQUIRE(compiled("1 != -1") == "( ( 1 ) != ( -( 1 ) ) ) \n");
  REQUIRE(compiled("1 != ~1") == "( ( 1 ) != ( ~( 1 ) ) ) \n");
  REQUIRE(compiled("1.0 == 1") == "( ( 1.000000 ) == ( 1 ) ) \n");
  REQUIRE(compiled("NAME == 1") == "( ( NAME[0] ) == ( 1 ) ) \n");
  REQUIRE(compiled("NAME[?] == 1") == "( ( NAME[?] ) == ( 1 ) ) \n");

  REQUIRE(
      compiled("FIRSTNAME %% 'J.*n' && SEX == 'M'") ==
      "( ( ( FIRSTNAME[0] ) %% ( 'J.*n' ) ) && ( ( SEX[0] ) == ( 'M' ) ) ) \n");
}
