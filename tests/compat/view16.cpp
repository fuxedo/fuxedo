// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>

#include <fml.h>
#include <xatmi.h>

#include <cstring>

#include "view16test.h"

TEST_CASE("tpalloc VIEW resolves the subtype through the view registry",
          "[view16]") {
  auto *buf = tpalloc(const_cast<char *>("VIEW"), const_cast<char *>("v16_all"),
                      0);
  REQUIRE(buf != nullptr);
  char type[16] = {0};
  char subtype[33] = {0};
  // pinned on Oracle 12.2.2: a VIEW allocation is at least the 1024-byte
  // platform default; the view definition must fit (larger views need
  // an explicit size)
  REQUIRE(tptypes(buf, type, subtype) == 1024);
  CHECK(strcmp(type, "VIEW") == 0);
  CHECK(strcmp(subtype, "v16_all") == 0);
  tpfree(buf);

  auto *cl = tpalloc(const_cast<char *>("VIEW"), const_cast<char *>("v16_cl"),
                     0);
  REQUIRE(cl != nullptr);
  memset(subtype, 0, sizeof(subtype));
  REQUIRE(tptypes(cl, type, subtype) == 1024);
  CHECK(strcmp(type, "VIEW") == 0);
  CHECK(strcmp(subtype, "v16_cl") == 0);
  tpfree(cl);
}

TEST_CASE("VIEW16 buffers are C structs: direct member access",
          "[view16]") {
  auto *buf = tpalloc(const_cast<char *>("VIEW"), const_cast<char *>("v16_all"),
                      0);
  REQUIRE(buf != nullptr);
  auto *v = reinterpret_cast<struct v16_all *>(buf);
  v->vs = -3;
  v->vl = 123456;
  v->vc = 'q';
  v->vf = 0.25f;
  v->vd = -9.5;
  strcpy(v->str, "payload");
  v->ca[0] = 'A';
  v->ca[1] = 'B';

  // the buffer survives a realloc with its bytes intact
  auto *grown = tprealloc(buf, 4 * 1024);
  REQUIRE(grown != nullptr);
  auto *g = reinterpret_cast<struct v16_all *>(grown);
  CHECK(g->vs == -3);
  CHECK(g->vl == 123456);
  CHECK(g->vc == 'q');
  CHECK(g->vf == 0.25f);
  CHECK(g->vd == -9.5);
  CHECK(strcmp(g->str, "payload") == 0);
  CHECK(g->ca[0] == 'A');
  CHECK(g->ca[1] == 'B');
  tpfree(grown);
}

TEST_CASE("VIEW16 counted members expose the ACM", "[view16]") {
  auto *buf = tpalloc(const_cast<char *>("VIEW"), const_cast<char *>("v16_cl"),
                      0);
  REQUIRE(buf != nullptr);
  auto *v = reinterpret_cast<struct v16_cl *>(buf);
  memset(v, 0, sizeof(*v));
  v->nums[0] = 1;
  v->nums[1] = 2;
  v->C_nums = 2;
  strcpy(v->names[0], "xy");
  v->L_names[0] = 2;
  CHECK(v->C_nums == 2);
  CHECK(v->nums[1] == 2);
  CHECK(v->L_names[0] == 2);
  tpfree(buf);
}

TEST_CASE("VIEW16 export/import roundtrips the struct bytes", "[view16]") {
  auto *buf = tpalloc(const_cast<char *>("VIEW"), const_cast<char *>("v16_all"),
                      0);
  REQUIRE(buf != nullptr);
  auto *v = reinterpret_cast<struct v16_all *>(buf);
  v->vs = 42;
  strcpy(v->str, "wire");

  char ostr[4 * 1024];
  long olen = sizeof(ostr);
  REQUIRE(tpexport(buf, 0, ostr, &olen, 0) == 1);
  REQUIRE(olen > 0);

  char *copy = nullptr;
  long ilen = olen;
  // 12.2 rejects a NULL target pointer with TPEINVAL, 22.1 allocates it
  if (tpimport(ostr, ilen, &copy, &olen, 0) == -1) {
    REQUIRE(tperrno == TPEINVAL);
    copy = tpalloc(const_cast<char *>("VIEW"), const_cast<char *>("v16_all"),
                   0);
    REQUIRE(copy != nullptr);
    olen = ilen;
    REQUIRE(tpimport(ostr, ilen, &copy, &olen, 0) == 1);
  }
  REQUIRE(copy != nullptr);
  auto *c = reinterpret_cast<struct v16_all *>(copy);
  CHECK(c->vs == 42);
  CHECK(strcmp(c->str, "wire") == 0);
  tpfree(copy);
  tpfree(buf);
}

TEST_CASE("tpalloc VIEW with an unknown view fails", "[view16]") {
  tperrno = 0;
  auto *buf = tpalloc(const_cast<char *>("VIEW"),
                      const_cast<char *>("no_such_view_16"), 0);
  CHECK(buf == nullptr);
  // pinned on Oracle 12.2.2: an unknown view name is TPEOS
  CHECK(tperrno == TPEOS);
}
