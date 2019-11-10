// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>

#include <xatmi.h>
#include <cstring>
#define DECONST(x) const_cast<char *>(x)

TEST_CASE("tpstrerror", "[xatmi]") {
  REQUIRE(strlen(tpstrerror(TPEABORT)) > 1);
  REQUIRE(strlen(tpstrerror(TPEBADDESC)) > 1);
  REQUIRE(strlen(tpstrerror(TPEBLOCK)) > 1);
  REQUIRE(strlen(tpstrerror(TPEINVAL)) > 1);
  REQUIRE(strlen(tpstrerror(TPELIMIT)) > 1);
  REQUIRE(strlen(tpstrerror(TPENOENT)) > 1);
  REQUIRE(strlen(tpstrerror(TPEOS)) > 1);
  REQUIRE(strlen(tpstrerror(TPEPERM)) > 1);
  REQUIRE(strlen(tpstrerror(TPEPROTO)) > 1);
  REQUIRE(strlen(tpstrerror(TPESVCERR)) > 1);
  REQUIRE(strlen(tpstrerror(TPESVCFAIL)) > 1);
  REQUIRE(strlen(tpstrerror(TPESYSTEM)) > 1);
  REQUIRE(strlen(tpstrerror(TPETIME)) > 1);
  REQUIRE(strlen(tpstrerror(TPETRAN)) > 1);
  REQUIRE(strlen(tpstrerror(TPGOTSIG)) > 1);
  REQUIRE(strlen(tpstrerror(TPERMERR)) > 1);
  REQUIRE(strlen(tpstrerror(TPEITYPE)) > 1);
  REQUIRE(strlen(tpstrerror(TPEOTYPE)) > 1);
  REQUIRE(strlen(tpstrerror(TPERELEASE)) > 1);
  REQUIRE(strlen(tpstrerror(TPEHAZARD)) > 1);
  REQUIRE(strlen(tpstrerror(TPEHEURISTIC)) > 1);
  REQUIRE(strlen(tpstrerror(TPEEVENT)) > 1);
  REQUIRE(strlen(tpstrerror(TPEMATCH)) > 1);
  REQUIRE(strlen(tpstrerror(TPEDIAGNOSTIC)) > 1);
  REQUIRE(strlen(tpstrerror(TPEMIB)) > 1);
}

TEST_CASE("tpalloc", "[tp-memory]") {
  GIVEN("NULL type") {
    WHEN("tpalloc is called") {
      char *ptr = tpalloc(NULL, DECONST("*"), 666);
      THEN("error is returned") {
        REQUIRE(ptr == nullptr);
        REQUIRE(tperrno == TPEINVAL);
        REQUIRE(strlen(tpstrerror(tperrno)) > 1);
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
        REQUIRE(strlen(tpstrerror(tperrno)) > 1);
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
        REQUIRE(strlen(tpstrerror(tperrno)) > 1);
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
