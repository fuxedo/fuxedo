#include <xatmi.h>
#include <catch.hpp>
#include <stdexcept>

TEST_CASE("blocktime seconds", "[xatmi]") {
  REQUIRE(tpsblktime(10, TPBLK_ALL) != -1);
  REQUIRE(tpsblktime(20, TPBLK_NEXT) != -1);
  REQUIRE(tpgblktime(TPBLK_ALL) == 10);
  REQUIRE(tpgblktime(TPBLK_NEXT) == 20);
}
TEST_CASE("blocktime milliseconds", "[xatmi]") {
  if (tpsblktime(10, TPBLK_ALL | TPBLK_MILLISECOND) == -1) {
    // 22.1 rejects sub-second granularities with TPEINVAL
    REQUIRE(tperrno == TPEINVAL);
    return;
  }
  REQUIRE(tpsblktime(20, TPBLK_NEXT | TPBLK_MILLISECOND) != -1);
  REQUIRE(tpgblktime(TPBLK_ALL | TPBLK_MILLISECOND) == 10);
  REQUIRE(tpgblktime(TPBLK_NEXT | TPBLK_MILLISECOND) == 20);
}
TEST_CASE("blocktime mix", "[xatmi]") {
  REQUIRE(tpsblktime(1, TPBLK_ALL) != -1);
  if (tpsblktime(2000, TPBLK_NEXT | TPBLK_MILLISECOND) == -1) {
    // 22.1 rejects sub-second granularities with TPEINVAL
    REQUIRE(tperrno == TPEINVAL);
    return;
  }
  REQUIRE(tpgblktime(TPBLK_ALL | TPBLK_MILLISECOND) == 1000);
  REQUIRE(tpgblktime(TPBLK_NEXT) == 2);
}
TEST_CASE("blocktime invalid arguments", "[xatmi]") {
  REQUIRE(tpsblktime(1, 0xa) == -1);
  REQUIRE(tperrno == TPEINVAL);
  REQUIRE(tpsblktime(1, 0xa0) == -1);
  REQUIRE(tperrno == TPEINVAL);
  REQUIRE(tpgblktime(0xa) == -1);
  REQUIRE(tperrno == TPEINVAL);
  REQUIRE(tpgblktime(0xa0) == -1);
  REQUIRE(tperrno == TPEINVAL);
}
