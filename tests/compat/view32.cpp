// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cmath>
#include <cstdio>
#include <cstring>

#include <catch.hpp>

#include <fml32.h>

#include "view32test.h"

static FLDID32 id(const char *name) {
  auto fid = Fldid32(const_cast<char *>(name));
  REQUIRE(fid != BADFLDID);
  return fid;
}

static FBFR32 *alloc() {
  auto *fb = Falloc32(16, 2048);
  REQUIRE(fb != nullptr);
  return fb;
}

TEST_CASE("Fvneeded32 matches the generated structures") {
  CHECK(Fvneeded32(const_cast<char *>("v_all")) == sizeof(struct v_all));
  CHECK(Fvneeded32(const_cast<char *>("v_cl")) == sizeof(struct v_cl));
  CHECK(Fvneeded32(const_cast<char *>("v_dir")) == sizeof(struct v_dir));
  CHECK(Fvneeded32(const_cast<char *>("v_expr")) == sizeof(struct v_expr));
  CHECK(Fvneeded32(const_cast<char *>("v_prop")) == sizeof(struct v_prop));
  CHECK(Fvneeded32(const_cast<char *>("v_conv")) == sizeof(struct v_conv));
}

TEST_CASE("Fvsinit32 initializes members to NULL values") {
  struct v_all v;
  memset(&v, 0xAA, sizeof v);
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_all")) == 1);
  CHECK(v.vs == 7);
  CHECK(v.vl == -1);
  CHECK(v.vc == 'Z');
  CHECK(v.vf == Approx(-0.5f));
  CHECK(v.vd == Approx(2.5));
  CHECK(strcmp(v.vstr, "hi") == 0);
  CHECK(v.vstr[2] == 0);
  CHECK(v.vca[0] == 0);
}

TEST_CASE("Fvsinit32 clears the low half of the ACM and sets the ALM") {
  struct v_cl v;
  memset(&v, 0xAA, sizeof v);
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_cl")) == 1);
  CHECK(v.C_nums == (int)0xAAAA0000);
  CHECK(v.nums[0] == 9);
  CHECK(v.L_names[0] == 2);
  CHECK(strncmp(v.names[0], "ab", 10) == 0);
}

TEST_CASE("Fvnull32 detects user and default NULL values") {
  struct v_prop v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_prop")) == 1);
  // the P flag propagates the last character of the NULL value on init
  CHECK(memcmp(v.street, "abccccccc", 9) == 0);
  CHECK(v.street[9] == 0);
  CHECK(memcmp(v.city, "xyz", 3) == 0);
  CHECK(v.city[3] == 0);

  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("street"), 0,
                 const_cast<char *>("v_prop")) == 1);
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("city"), 0,
                 const_cast<char *>("v_prop")) == 1);

  // P flag: Fvnull32() compares against the sinit fill byte for byte
  memset(v.street, 'c', sizeof v.street);
  memcpy(v.street, "ab", 2);
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("street"), 0,
                 const_cast<char *>("v_prop")) == 0);
  v.street[9] = 0;
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("street"), 0,
                 const_cast<char *>("v_prop")) == 1);
  v.street[3] = 'd';
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("street"), 0,
                 const_cast<char *>("v_prop")) == 0);

  // without P the remainder must be NUL
  memset(v.city, 0, sizeof v.city);
  memcpy(v.city, "xyz", 3);
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("city"), 0,
                 const_cast<char *>("v_prop")) == 1);
  v.city[3] = 'q';
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("city"), 0,
                 const_cast<char *>("v_prop")) == 0);

  // the P-padded fill is not NULL for Fvstof32() which ignores P
  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_prop")) == 1);
  CHECK(Foccur32(fb, id("VESTREET")) == 1);
  Ffree32(fb);
}

TEST_CASE("Fvnull32 NONE means nothing is NULL") {
  struct v_conv v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_conv")) == 1);
  // the total member has NONE as its NULL value
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("total"), 0,
                 const_cast<char *>("v_conv")) == 0);
  v.total = 0;
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("total"), 0,
                 const_cast<char *>("v_conv")) == 0);
  v.total = 5;
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("total"), 0,
                 const_cast<char *>("v_conv")) == 0);

  // an ordinary member is NULL when it equals its NULL value
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("salary"), 0,
                 const_cast<char *>("v_conv")) == 1);
  v.salary = 5;
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("salary"), 0,
                 const_cast<char *>("v_conv")) == 0);
}

TEST_CASE("Fvstof32 FUPDATE transfers non-NULL members only") {
  struct v_all v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_all")) == 1);
  v.vs = 1;
  v.vl = 5;
  v.vc = 'A';
  strcpy(v.vstr, "hello");
  // v.vf, v.vd, v.vca stay NULL and must not be transferred

  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_all")) == 1);
  CHECK(Foccur32(fb, id("VSHORT")) == 1);
  CHECK(Foccur32(fb, id("VLONG")) == 1);
  CHECK(Foccur32(fb, id("VCHAR")) == 1);
  CHECK(Foccur32(fb, id("VSTRING")) == 1);
  CHECK(Foccur32(fb, id("VFLOAT")) == 0);
  CHECK(Foccur32(fb, id("VDOUBLE")) == 0);
  CHECK(Foccur32(fb, id("VCARRAY")) == 0);
  Ffree32(fb);
}

TEST_CASE("Fvstof32 FCONCAT appends to the buffer") {
  struct v_all v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_all")) == 1);
  v.vs = 3;
  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_all")) == 1);
  v.vs = 4;
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FCONCAT,
                   const_cast<char *>("v_all")) == 1);
  CHECK(Foccur32(fb, id("VSHORT")) == 2);
  short s = 0;
  FLDLEN32 len = sizeof(s);
  REQUIRE(Fget32(fb, id("VSHORT"), 1, reinterpret_cast<char *>(&s), &len) != -1);
  CHECK(s == 4);
  Ffree32(fb);
}

TEST_CASE("Fvstof32 honors the ACM") {
  struct v_cl v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_cl")) == 1);
  v.nums[0] = 1;
  v.nums[1] = 2;
  v.nums[2] = 3;
  v.C_nums = 2;
  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_cl")) == 1);
  CHECK(Foccur32(fb, id("VNUMS")) == 2);
  CHECK(v.C_nums == 2);

  v.C_nums = -1;
  CHECK(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                 const_cast<char *>("v_cl")) == -1);
  CHECK(Ferror32 == FBADACM);
  Ffree32(fb);
}

TEST_CASE("Fvftos32 transfers occurrences and reports overflow in the ACM") {
  auto *fb = alloc();
  short vals[] = {11, 22, 33, 44};
  for (auto v : vals) {
    REQUIRE(Fadd32(fb, id("VNUMS"), reinterpret_cast<char *>(&v), 0) != -1);
  }
  REQUIRE(Fadd32(fb, id("VNAMES"), const_cast<char *>("hello"), 0) != -1);
  REQUIRE(Fadd32(fb, id("VNAMES"), const_cast<char *>("a much longer string"), 0) != -1);

  struct v_cl v;
  REQUIRE(Fvftos32(fb, reinterpret_cast<char *>(&v), const_cast<char *>("v_cl")) == 1);
  CHECK(v.C_nums == -1);
  CHECK(v.nums[0] == 11);
  CHECK(v.nums[1] == 22);
  CHECK(v.nums[2] == 33);
  CHECK(v.C_names == 2);
  CHECK(strncmp(v.names[0], "hello", 10) == 0);
  // the second string is truncated to the member size, the ALM keeps the
  // original length
  // strings are copied like strlcpy: at most elsize - 1 bytes + terminator
  CHECK(strncmp(v.names[1], "a much lo", 9) == 0);
  CHECK(v.names[1][9] == 0);
  // the ALM keeps the full field length including the terminator
  CHECK(v.L_names[0] == 6);
  CHECK(v.L_names[1] == 21);
  Ffree32(fb);
}

TEST_CASE("Fvftos32 fills missing members with NULL values") {
  auto *fb = alloc();
  long l = 42;
  REQUIRE(Fadd32(fb, id("VLONG"), reinterpret_cast<char *>(&l), 0) != -1);

  struct v_all v;
  REQUIRE(Fvftos32(fb, reinterpret_cast<char *>(&v), const_cast<char *>("v_all")) == 1);
  CHECK(v.vl == 42);
  CHECK(v.vs == 7);
  CHECK(v.vc == 'Z');
  CHECK(strcmp(v.vstr, "hi") == 0);
  Ffree32(fb);
}

TEST_CASE("carray length member controls the bytes transferred") {
  struct v_cl v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_cl")) == 1);
  memcpy(v.items[0], "ABCDEFGH", 8);
  memcpy(v.items[1], "IJKLMNOP", 8);
  v.C_items = 2;
  v.L_items[0] = 3;
  v.L_items[1] = 0;  // zero-length carray is transferred

  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_cl")) == 1);
  CHECK(Foccur32(fb, id("VITEMS")) == 2);
  FLDLEN32 len = 0;
  auto *val = Ffind32(fb, id("VITEMS"), 0, &len);
  REQUIRE(val != nullptr);
  CHECK(len == 3);
  CHECK(memcmp(val, "ABC", 3) == 0);
  val = Ffind32(fb, id("VITEMS"), 1, &len);
  REQUIRE(val != nullptr);
  CHECK(len == 0);

  // and back: the ALM reflects the field lengths
  char buf[sizeof(struct v_cl)];
  REQUIRE(Fvftos32(fb, buf, const_cast<char *>("v_cl")) == 1);
  auto *back = reinterpret_cast<struct v_cl *>(buf);
  CHECK(back->L_items[0] == 3);
  CHECK(back->L_items[1] == 0);
  CHECK(memcmp(back->items[0], "ABC", 3) == 0);
  Ffree32(fb);
}

TEST_CASE("direction flags restrict the transfer") {
  struct v_dir v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_dir")) == 1);
  v.ftob = 5;
  v.btos = 7;
  v.none = 9;

  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_dir")) == 1);
  CHECK(Foccur32(fb, id("VLONG")) == 1);
  CHECK(Foccur32(fb, id("VEAGE")) == 0);
  CHECK(Foccur32(fb, id("VCOUNT")) == 0);

  // btos is buffer-to-structure only, ftos structure-to-buffer only
  long l = 100;
  REQUIRE(Fadd32(fb, id("VEAGE"), reinterpret_cast<char *>(&l), 0) != -1);
  struct v_dir out;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&out), const_cast<char *>("v_dir")) == 1);
  REQUIRE(Fvftos32(fb, reinterpret_cast<char *>(&out),
                   const_cast<char *>("v_dir")) == 1);
  CHECK(out.ftob == 0);
  CHECK(out.btos == 100);
  CHECK(out.none == 0);
  Ffree32(fb);
}

TEST_CASE("Fvopt32 changes mapping options at run time") {
  struct v_dir v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_dir")) == 1);
  v.ftob = 5;
  v.none = 9;

  CHECK(Fvopt32(nullptr, F_BOTH, const_cast<char *>("v_dir")) == -1);
  CHECK(Fvopt32(const_cast<char *>("ftob"), 999,
                const_cast<char *>("v_dir")) == -1);
  CHECK(Ferror32 == FEINVAL);

  REQUIRE(Fvopt32(const_cast<char *>("ftob"), F_OFF,
                  const_cast<char *>("v_dir")) == 1);
  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_dir")) == 1);
  CHECK(Foccur32(fb, id("VLONG")) == 0);

  REQUIRE(Fvopt32(const_cast<char *>("none"), F_BOTH,
                  const_cast<char *>("v_dir")) == 1);
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_dir")) == 1);
  CHECK(Foccur32(fb, id("VLONG")) == 0);  // ftob is still off
  CHECK(Foccur32(fb, id("VCOUNT")) == 1);  // none member now maps too

  CHECK(Fvopt32(const_cast<char *>("nosuch"), F_OFF,
                const_cast<char *>("v_dir")) == -1);
  CHECK(Ferror32 == FNOCNAME);
  Ffree32(fb);
}

TEST_CASE("Fvselinit32 initializes a single member") {
  struct v_all v;
  memset(&v, 0xAA, sizeof v);
  REQUIRE(Fvselinit32(reinterpret_cast<char *>(&v), const_cast<char *>("vstr"),
                      const_cast<char *>("v_all")) == 1);
  CHECK(strcmp(v.vstr, "hi") == 0);
  CHECK(v.vs == (short)0xAAAA);

  CHECK(Fvselinit32(reinterpret_cast<char *>(&v), const_cast<char *>("nosuch"),
                    const_cast<char *>("v_all")) == -1);
  CHECK(Ferror32 == FNOCNAME);
}

TEST_CASE("types are converted between the buffer and the structure") {
  struct v_conv v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_conv")) == 1);
  v.salary = 5000;
  strcpy(v.desc, "note");

  auto *fb = alloc();
  REQUIRE(Fvstof32(fb, reinterpret_cast<char *>(&v), FUPDATE,
                   const_cast<char *>("v_conv")) == 1);
  // long member to float field
  FLDLEN32 len = 0;
  auto *f = Ffind32(fb, id("VSALARY"), 0, &len);
  REQUIRE(f != nullptr);
  CHECK(*reinterpret_cast<float *>(f) == Approx(5000));
  CHECK(Foccur32(fb, id("VDESC")) == 1);

  // float field converted to a long member
  Fdelall32(fb, id("VSALARY"));
  float fl = 123.5f;
  REQUIRE(Fadd32(fb, id("VSALARY"), reinterpret_cast<char *>(&fl), 0) != -1);
  char buf[sizeof(struct v_conv)];
  REQUIRE(Fvftos32(fb, buf, const_cast<char *>("v_conv")) == 1);
  auto *back = reinterpret_cast<struct v_conv *>(buf);
  CHECK(back->salary == 123);
  Ffree32(fb);
}

TEST_CASE("boolean expressions over views") {
  struct v_expr v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_expr")) == 1);
  strcpy(v.ename, "Alice");
  v.eage = 30;
  v.emark = 4.5f;

  char *tree = Fvboolco32(const_cast<char *>("eage > 18 && ename == 'Alice'"),
                          const_cast<char *>("v_expr"));
  REQUIRE(tree != nullptr);
  CHECK(Fvboolev32(reinterpret_cast<char *>(&v), tree,
                   const_cast<char *>("v_expr")) == 1);
  v.eage = 12;
  CHECK(Fvboolev32(reinterpret_cast<char *>(&v), tree,
                   const_cast<char *>("v_expr")) == 0);
  v.eage = 30;
  CHECK(Fvfloatev32(reinterpret_cast<char *>(&v),
                    Fvboolco32(const_cast<char *>("emark * 2"),
                               const_cast<char *>("v_expr")),
                    const_cast<char *>("v_expr")) == Approx(9.0));

  char file[256];
  auto *f = fmemopen(file, sizeof file, "w");
  REQUIRE(f != nullptr);
  REQUIRE(Fvboolpr32(tree, f, const_cast<char *>("v_expr")) == 0);
  fclose(f);
  CHECK(strstr(file, "eage") != nullptr);

  CHECK(Fvboolco32(const_cast<char *>("nosuch > 1"),
                   const_cast<char *>("v_expr")) == nullptr);
  CHECK(Ferror32 == FBADNAME);
  CHECK(Fvboolco32(const_cast<char *>("C_names == 2"),
                   const_cast<char *>("v_cl")) == nullptr);
  CHECK(Ferror32 == FBADNAME);
  CHECK(Fvboolco32(const_cast<char *>("eage > 1"),
                   const_cast<char *>("nosuchview")) == nullptr);
  CHECK(Ferror32 == FBADVIEW);
}

TEST_CASE("error handling") {
  CHECK(Fvneeded32(const_cast<char *>("nosuchview")) == -1);
  CHECK(Ferror32 == FBADVIEW);

  struct v_all v;
  REQUIRE(Fvsinit32(reinterpret_cast<char *>(&v), const_cast<char *>("v_all")) == 1);
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("nosuch"), 0,
                 const_cast<char *>("v_all")) == -1);
  CHECK(Ferror32 == FNOCNAME);
  CHECK(Fvnull32(reinterpret_cast<char *>(&v), const_cast<char *>("vs"), 5,
                 const_cast<char *>("v_all")) == 1);
  CHECK(Ferror32 == FNOCNAME);

  auto *fb = alloc();
  CHECK(Fvstof32(fb, reinterpret_cast<char *>(&v), 42,
                 const_cast<char *>("v_all")) == -1);
  CHECK(Ferror32 == FEINVAL);
  CHECK(Fvstof32(nullptr, reinterpret_cast<char *>(&v), FUPDATE,
                 const_cast<char *>("v_all")) == -1);
  CHECK(Ferror32 == FALIGNERR);
  CHECK(Fvftos32(fb, reinterpret_cast<char *>(&v),
                 const_cast<char *>("nosuchview")) == -1);
  CHECK(Ferror32 == FBADVIEW);
  Ffree32(fb);
}

TEST_CASE("a missing VIEWFILES32 means the view can not be found") {
  auto *files = getenv("VIEWFILES32");
  REQUIRE(files != nullptr);
  unsetenv("VIEWFILES32");
  Fvrefresh32();
  CHECK(Fvneeded32(const_cast<char *>("v_all")) == -1);
  CHECK(Ferror32 == FVFOPEN);
  setenv("VIEWFILES32", files, 1);
  Fvrefresh32();
  CHECK(Fvneeded32(const_cast<char *>("v_all")) == sizeof(struct v_all));
  CHECK(Fvneeded32(const_cast<char *>("nosuchview")) == -1);
  CHECK(Ferror32 == FBADVIEW);
}
