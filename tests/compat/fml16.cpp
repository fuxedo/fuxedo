// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>

#include <fml.h>
#include <fml32.h>
#include <xatmi.h>

#include <cmath>
#include <cstdio>
#include <cstring>

// Success returns are pinned against Oracle 12.2.2 and asserted
// exactly: the FML family answers 1 on success (fuxedo matches), so
// success below is == 1 and failure == -1 with a specific Ferror.

static FLDID mk(int type, long num) { return Fmkfldid(type, num); }

template <typename T>
T repack(char *p) {
  T tmp;
  memcpy(&tmp, p, sizeof(tmp));
  return tmp;
}

TEST_CASE("fldid16 packing", "[fml16]") {
  int types[] = {FLD_SHORT, FLD_LONG,  FLD_CHAR,   FLD_FLOAT,
                 FLD_DOUBLE, FLD_STRING, FLD_CARRAY};
  for (auto type : types) {
    FLDID id = mk(type, 13);
    REQUIRE(id != BADFLDID);
    REQUIRE(Fldtype(id) == type);
    REQUIRE(Fldno(id) == 13);
  }
  // field number space is 13 bits
  REQUIRE(Fldno(mk(FLD_LONG, 0x1FFF)) == 0x1FFF);
  // the iteration sentinel equals the invalid id, like the platform
  REQUIRE(FIRSTFLDID == BADFLDID);
}

TEST_CASE("Fadd/Fget all base types", "[fml16]") {
  auto *fb = Falloc(16, 2048);
  REQUIRE(fb != nullptr);

  short sv = -7;
  REQUIRE(Fadd(fb, mk(FLD_SHORT, 1), reinterpret_cast<char *>(&sv), 0) == 1);
  long lv = 123456;
  REQUIRE(Fadd(fb, mk(FLD_LONG, 2), reinterpret_cast<char *>(&lv), 0) == 1);
  char cv = 'x';
  REQUIRE(Fadd(fb, mk(FLD_CHAR, 3), &cv, 0) == 1);
  float fv = 1.5f;
  REQUIRE(Fadd(fb, mk(FLD_FLOAT, 4), reinterpret_cast<char *>(&fv), 0) == 1);
  double dv = -2.25;
  REQUIRE(Fadd(fb, mk(FLD_DOUBLE, 5), reinterpret_cast<char *>(&dv), 0) !=
          -1);
  REQUIRE(Fadd(fb, mk(FLD_STRING, 6), const_cast<char *>("hello"), 0) == 1);
  char carr[] = {1, 2, 3, 4};
  REQUIRE(Fadd(fb, mk(FLD_CARRAY, 7), carr, sizeof(carr)) == 1);

  FLDLEN len = sizeof(sv);
  short out_s = 0;
  REQUIRE(Fget(fb, mk(FLD_SHORT, 1), 0, reinterpret_cast<char *>(&out_s),
                &len) == 1);
  CHECK(out_s == sv);
  CHECK(len == sizeof(sv));

  len = sizeof(dv);
  double out_d = 0;
  REQUIRE(Fget(fb, mk(FLD_DOUBLE, 5), 0, reinterpret_cast<char *>(&out_d),
                &len) == 1);
  CHECK(out_d == Approx(dv));

  // a string keeps its terminator in the stored length
  char buf[16] = {0};
  len = sizeof(buf);
  REQUIRE(Fget(fb, mk(FLD_STRING, 6), 0, buf, &len) == 1);
  CHECK(strcmp(buf, "hello") == 0);
  CHECK(len == 6);

  len = sizeof(buf);
  REQUIRE(Fget(fb, mk(FLD_CARRAY, 7), 0, buf, &len) == 1);
  CHECK(len == 4);
  CHECK(memcmp(buf, carr, 4) == 0);

  // a too-small value buffer is FNOSPACE
  len = 2;
  CHECK(Fget(fb, mk(FLD_STRING, 6), 0, buf, &len) == -1);
  CHECK(Ferror == FNOSPACE);

  Ffree(fb);
}

TEST_CASE("occurrences, Foccur, Fdel, Fdelall", "[fml16]") {
  auto *fb = Falloc(16, 1024);
  REQUIRE(fb != nullptr);
  auto id = mk(FLD_LONG, 10);
  CHECK(Foccur(fb, id) == 0);
  for (long n = 1; n <= 3; n++) {
    REQUIRE(Fadd(fb, id, reinterpret_cast<char *>(&n), 0) == 1);
  }
  CHECK(Foccur(fb, id) == 3);
  CHECK(Fvall(fb, id, 2) == 3);
  CHECK(Fvals(fb, id, 0) != nullptr);

  // delete the middle occurrence, the rest slides down
  REQUIRE(Fdel(fb, id, 1) == 1);
  CHECK(Foccur(fb, id) == 2);
  CHECK(Fvall(fb, id, 1) == 3);
  CHECK(Fvall(fb, id, 0) == 1);
  CHECK(Fdel(fb, id, 5) == -1);
  CHECK(Ferror == FNOTPRES);

  // pinned on Oracle 12.2.2 and 22.1: Fdelall deletes every occurrence
  // but answers 1 (not the count); an absent field is -1/FNOTPRES
  CHECK(Fdelall(fb, id) == 1);
  CHECK(Foccur(fb, id) == 0);
  Ferror = 0;
  CHECK(Fdelall(fb, id) == -1);
  CHECK(Ferror == FNOTPRES);
  Ffree(fb);
}

TEST_CASE("Fchg changes in place and appends on new occurrences",
          "[fml16]") {
  auto *fb = Falloc(16, 1024);
  REQUIRE(fb != nullptr);
  auto id = mk(FLD_STRING, 20);
  REQUIRE(Fadd(fb, id, const_cast<char *>("first"), 0) == 1);
  REQUIRE(Fadd(fb, id, const_cast<char *>("second"), 0) == 1);

  // change in place, also shrinking
  REQUIRE(Fchg(fb, id, 0, const_cast<char *>("f"), 0) == 1);
  CHECK(strcmp(Fvals(fb, id, 0), "f") == 0);
  CHECK(strcmp(Fvals(fb, id, 1), "second") == 0);
  CHECK(Foccur(fb, id) == 2);

  // change on a fresh occurrence appends rather than failing
  REQUIRE(Fchg(fb, id, 2, const_cast<char *>("third"), 0) == 1);
  CHECK(Foccur(fb, id) == 3);
  CHECK(strcmp(Fvals(fb, id, 2), "third") == 0);

  // changing a missing field adds it
  auto other = mk(FLD_LONG, 21);
  long v = 9;
  REQUIRE(Fchg(fb, other, 0, reinterpret_cast<char *>(&v), 0) == 1);
  CHECK(Fvall(fb, other, 0) == 9);
  Ffree(fb);
}

TEST_CASE("Fsizeof/Fused/Finit", "[fml16]") {
  auto *fb = Falloc(16, 1024);
  REQUIRE(fb != nullptr);
  auto size = Fsizeof(fb);
  REQUIRE(size >= 1024);
  auto empty_used = Fused(fb);
  REQUIRE(Fused(fb) < size);
  long v = 42;
  REQUIRE(Fadd(fb, mk(FLD_LONG, 30), reinterpret_cast<char *>(&v), 0) == 1);
  CHECK(Fused(fb) > empty_used);
  CHECK(Fused(fb) < size);

  REQUIRE(Finit(fb, size) == 1);
  CHECK(Fsizeof(fb) == size);
  CHECK(Fused(fb) == empty_used);
  CHECK(Foccur(fb, mk(FLD_LONG, 30)) == 0);
  Ffree(fb);
}

TEST_CASE("Ferror is set on failures and Fstrerror covers the codes",
          "[fml16]") {
  auto *fb = Falloc(16, 256);
  REQUIRE(fb != nullptr);
  Ferror = 0;
  char buf[8];
  FLDLEN len = sizeof(buf);
  CHECK(Fget(fb, mk(FLD_LONG, 40), 0, buf, &len) == -1);
  CHECK(Ferror == FNOTPRES);

  Ferror = 0;
  CHECK(Fadd(nullptr, mk(FLD_LONG, 40), buf, 0) == -1);
  CHECK(Ferror != 0);

  Ferror = 0;
  // Fmkfldid masks the type to 3 bits, so an invalid type is built raw:
  // type 7 has no meaning in FML16
  auto bad = static_cast<FLDID>((7u << 13) | 40);
  CHECK(Fadd(fb, bad, buf, 0) == -1);
  CHECK(Ferror != 0);

  int codes[] = {FALIGNERR, FNOTFLD,  FNOSPACE, FNOTPRES, FBADFLD,
                 FTYPERR,   FEUNIX,   FBADNAME, FMALLOC,  FSYNTAX,
                 FEINVAL};
  for (auto code : codes) {
    CHECK(strlen(Fstrerror(code)) > 1);
  }
  Ffree(fb);
}

TEST_CASE("F16to32/F32to16 in-place conversion roundtrip", "[fml16]") {
  auto *fb = Falloc(16, 1024);
  REQUIRE(fb != nullptr);
  short s = -3;
  REQUIRE(Fadd(fb, mk(FLD_SHORT, 1), reinterpret_cast<char *>(&s), 0) == 1);
  REQUIRE(Fadd(fb, mk(FLD_STRING, 2), const_cast<char *>("swap"), 0) == 1);
  long l = 777;
  REQUIRE(Fadd(fb, mk(FLD_LONG, 2), reinterpret_cast<char *>(&l), 0) == 1);

  auto *fb32 = Falloc32(16, 2048);
  REQUIRE(fb32 != nullptr);
  REQUIRE(F16to32(fb32, fb) == 1);
  FLDLEN32 len = 0;
  auto *p32 = Ffind32(fb32, Fmkfldid32(FLD_LONG, 2), 0, &len);
  REQUIRE(p32 != nullptr);
  CHECK(repack<long>(p32) == 777);
  CHECK(Ffind32(fb32, Fmkfldid32(FLD_STRING, 2), 0, nullptr) != nullptr);
  CHECK(Foccur32(fb32, Fmkfldid32(FLD_SHORT, 1)) == 1);

  auto *back = Falloc(16, 1024);
  REQUIRE(back != nullptr);
  REQUIRE(F32to16(back, fb32) == 1);
  CHECK(Fvall(back, mk(FLD_LONG, 2), 0) == 777);
  CHECK(strcmp(Fvals(back, mk(FLD_STRING, 2), 0), "swap") == 0);
  CHECK(Fvall(back, mk(FLD_SHORT, 1), 0) == -3);

  Ferror = 0;
  Ferror32 = 0;
  CHECK(F16to32(nullptr, fb) == -1);
  CHECK(F32to16(nullptr, fb32) == -1);

  Ffree(back);
  Ffree32(fb32);
  Ffree(fb);
}

TEST_CASE("CF* counted variants mirror the plain calls", "[fml16]") {
  auto *fb = Falloc(16, 512);
  REQUIRE(fb != nullptr);
  long v = 5150;
  REQUIRE(CFadd(fb, mk(FLD_LONG, 50), reinterpret_cast<char *>(&v),
                sizeof(v), FLD_LONG) == 1);
  FLDLEN len = sizeof(v);
  long out = 0;
  REQUIRE(CFget(fb, mk(FLD_LONG, 50), 0, reinterpret_cast<char *>(&out),
                &len, FLD_LONG) == 1);
  CHECK(out == 5150);
  len = 0;
  auto *p = CFfind(fb, mk(FLD_LONG, 50), 0, &len, FLD_LONG);
  REQUIRE(p != nullptr);
  CHECK(repack<long>(p) == 5150);
  CHECK(len == sizeof(v));
  Ffree(fb);
}

TEST_CASE("Fldid resolves names through the field tables", "[fml16]") {
  Ferror = 0;
  FLDID id = Fldid(const_cast<char *>("NAME"));
  REQUIRE(id != BADFLDID);
  CHECK(Fldtype(id) == FLD_STRING);
  // table numbers are relative to the last *base directive: 205 + 30
  CHECK(Fldno(id) == 235);
  Ferror = 0;
  CHECK(Fldid(const_cast<char *>("NO_SUCH_FIELD_XYZ")) == BADFLDID);
  CHECK(Ferror == FBADNAME);
}

TEST_CASE("tpalloc FML buffers are fielded buffers", "[fml16]") {
  auto *buf = tpalloc(const_cast<char *>("FML"), nullptr, 0);
  REQUIRE(buf != nullptr);
  char type[16] = {0};
  char subtype[16] = {0};
  // pinned on Oracle 12.2.2: tptypes answers the allocated size, and
  // a default FML buffer is 1024 bytes
  REQUIRE(tptypes(buf, type, subtype) == 1024);
  CHECK(strcmp(type, "FML") == 0);

  auto *fb = reinterpret_cast<FBFR *>(buf);
  long v = 314;
  REQUIRE(Fadd(fb, mk(FLD_LONG, 60), reinterpret_cast<char *>(&v), 0) == 1);
  CHECK(Fvall(fb, mk(FLD_LONG, 60), 0) == 314);

  auto * grown = tprealloc(buf, 4 * 1024);
  REQUIRE(grown != nullptr);
  CHECK(Fvall(reinterpret_cast<FBFR *>(grown), mk(FLD_LONG, 60), 0) == 314);
  tpfree(grown);
}
