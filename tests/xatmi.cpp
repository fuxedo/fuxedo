// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>

#include <xatmi.h>
#include <cstring>
#define DECONST(x) const_cast<char *>(x)

TEST_CASE("tpalloc", "[tp-memory]") {
  GIVEN("NULL type") {
    WHEN("tpalloc is called") {
      char *ptr = tpalloc(NULL, DECONST("*"), 666);
      THEN("error is returned") {
        REQUIRE(ptr == nullptr);
        REQUIRE(tperrno == TPEINVAL);
      }
    }
  }

  GIVEN("STRING type") {
    WHEN("tpalloc is called") {
      char *ptr = tpalloc(DECONST("STRING"), DECONST("*"), 666);
      THEN("it succeeds") { REQUIRE(ptr != nullptr); }
      THEN("memory is writable") { memset(ptr, 0, 666); }
    }
  }

  GIVEN("CARRAY type") {
    WHEN("tpalloc is called") {
      char *ptr = tpalloc(DECONST("CARRAY"), DECONST("*"), 10);
      THEN("it succeeds") { REQUIRE(ptr != nullptr); }
      THEN("memory is writable") { memset(ptr, 0, 10); }
    }
  }

  GIVEN("any type (CARRAY in this case)") {
    WHEN("tpalloc is called") {
      char *ptr = tpalloc(DECONST("CARRAY"), DECONST("*"), 666);
      THEN("returns a buffer aligned on long workd") {
        REQUIRE(((long)ptr % sizeof(long)) == 0);
      }
    }
  }

  GIVEN("FOOBAR type") {
    WHEN("tpalloc is called") {
      char *ptr = tpalloc(DECONST("FOOBAR"), DECONST("*"), 666);
      THEN("error is returned") {
        REQUIRE(ptr == nullptr);
        REQUIRE(tperrno == TPENOENT);
      }
    }
  }
}

TEST_CASE("tptypes", "[tp-memory]") {
  GIVEN("NULL buffer") {
    char *ptr = NULL;
    WHEN("tptypes is called") {
      char type[8];
      char subtype[16];
      THEN("error is returned") {
        REQUIRE(tptypes(ptr, type, subtype) == -1);
        REQUIRE(tperrno == TPEINVAL);
      }
    }
  }

  GIVEN("STRING buffer") {
    char *ptr = tpalloc(DECONST("STRING"), DECONST("*"), 666);
    WHEN("tptypes is called") {
      char type[8];
      char subtype[16];
      THEN("STRING type is returned") {
        REQUIRE(tptypes(ptr, type, subtype) != -1);
        REQUIRE(strcmp(type, "STRING") == 0);
        REQUIRE(strcmp(subtype, "*") == 0);
      }
    }
  }
}
