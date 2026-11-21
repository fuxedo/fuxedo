// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// API hardening: invalid arguments must fail cleanly (no crash) for the
// FML32, VIEW32, and XATMI entry points. Only safe invalid values are
// exercised - null pointers, -1/0, unknown names, bad flags - never
// dangling pointers. Runs against fuxedo and Oracle Tuxedo; assertions
// are tolerant where the platforms pin different error codes.

#include <catch.hpp>

#include <fml32.h>
#include <xatmi.h>
#include <cstring>
#include <string>

#define DECONST(x) const_cast<char *>(x)

namespace {

FBFR32 *mkfbfr() {
  auto *fbfr = Falloc32(100, 100);
  REQUIRE(fbfr != nullptr);
  return fbfr;
}

// accepts the "invalid argument" Ferror codes seen across the
// platforms: FALIGNERR for null buffers (Oracle 12.2, probed),
// FNOTFLD on some paths, FEINVAL elsewhere
bool bad_args() {
  return Ferror32 == FALIGNERR || Ferror32 == FNOTFLD || Ferror32 == FEINVAL;
}

}  // namespace

TEST_CASE("FML32 argument validation", "[hardening]") {
  auto *fbfr = mkfbfr();
  auto NAME = Fldid32(DECONST("NAME"));
  char buf[64] = {};

  // null buffer
  REQUIRE(Fadd32(nullptr, NAME, DECONST("x"), 0) == -1);
  REQUIRE(bad_args());
  REQUIRE(Fchg32(nullptr, NAME, 0, DECONST("x"), 0) == -1);
  REQUIRE(Fget32(nullptr, NAME, 0, buf, nullptr) == -1);
  REQUIRE(Fdel32(nullptr, NAME, 0) == -1);
  REQUIRE(Foccur32(nullptr, NAME) == -1);
  REQUIRE(Fnext32(nullptr, nullptr, nullptr, nullptr, nullptr) == -1);
  REQUIRE(Fprint32(nullptr) == -1);
  REQUIRE(Fsizeof32(nullptr) == -1);
  REQUIRE(Fused32(nullptr) == -1);
  REQUIRE(Fchksum32(nullptr) == -1);

  // bad field ids
  REQUIRE(Fadd32(fbfr, BADFLDID, DECONST("x"), 0) == -1);
  REQUIRE(Fadd32(fbfr, 0, DECONST("x"), 0) == -1);
  REQUIRE(Fget32(fbfr, BADFLDID, 0, buf, nullptr) == -1);

  // bad occurrence
  // a negative occurrence on Fchg32 appends (Fadd semantics), matching
  // the platform; Fdel32 with -1 fails (its Ferror is left unset there)
  REQUIRE(Fchg32(fbfr, NAME, -1, DECONST("x"), 0) != -1);
  REQUIRE(Foccur32(fbfr, NAME) == 1);
  REQUIRE(Fdel32(fbfr, NAME, -2) == -1);

  // null value where a value is required: Fadd/Fget reject it
  // null value where a value is required: Fadd rejects it
  REQUIRE(Fadd32(fbfr, NAME, nullptr, 0) == -1);

  // null value with null length is a presence check on the platform:
  // 1 when the field exists (this fbfr has NAME), -1 when it does not
  REQUIRE(Fget32(fbfr, NAME, 0, nullptr, nullptr) == 1);
  auto *fresh = mkfbfr();
  REQUIRE(Fget32(fresh, NAME, 0, nullptr, nullptr) == -1);
  Ffree32(fresh);

  // Fchg32 with a null value deletes the occurrence and reports the
  // delete result (1 on success, pinned against Oracle 12.2)
  auto *fbfr2 = mkfbfr();
  REQUIRE(Fadd32(fbfr2, NAME, DECONST("v"), 0) != -1);
  REQUIRE(Fchg32(fbfr2, NAME, 0, nullptr, 0) == 1);
  REQUIRE(Foccur32(fbfr2, NAME) == 0);
  REQUIRE(Fchg32(fbfr2, NAME, 5, nullptr, 0) == -1);
  Ffree32(fbfr2);

  // null extra buffer arguments
  REQUIRE(Fcpy32(fbfr, nullptr) == -1);
  REQUIRE(Fcpy32(nullptr, fbfr) == -1);
  REQUIRE(Fconcat32(fbfr, nullptr) == -1);
  REQUIRE(Fjoin32(fbfr, nullptr) == -1);
  REQUIRE(Fojoin32(fbfr, nullptr) == -1);
  // NOTE: Fproj32(fbfr, NULL) segfaults on Oracle 12.2 itself (probed
  // in gdb); fuxedo returns -1 with FEINVAL. Not exercised here so the
  // suite stays runnable against both.

  // Fdel32 on a field that does not exist fails
  auto *fbfr3 = mkfbfr();
  REQUIRE(Fdel32(fbfr3, NAME, 0) == -1);
  Ffree32(fbfr3);

  // type helpers
  // a type-0 (FLD_SHORT) field id is accepted by both platforms; only
  // the bare fieldid 0 is rejected (pinned against Oracle 12.2)
  REQUIRE(Fadd32(fbfr, Fmkfldid32(0, 10), DECONST("1"), 0) != -1);
  REQUIRE(Fldid32(DECONST("no_such_field_anywhere")) == BADFLDID);
  REQUIRE(Fldno32(BADFLDID) == 0);

  // boolean expressions already pin null handling; here just the
  // unknown-field shape
  REQUIRE(Fboolco32(DECONST("no_such_field == 1")) == nullptr);
  REQUIRE(Ferror32 == FBADNAME);

  Ffree32(fbfr);
}

TEST_CASE("VIEW32 argument validation", "[hardening]") {
  char cstruct[4096] = {};

  // unknown views fail everywhere
  REQUIRE(Fvsinit32(cstruct, DECONST("no_such_view")) == -1);
  REQUIRE(Ferror32 == FBADVIEW);
  REQUIRE(Fvnull32(cstruct, nullptr, 0, DECONST("no_such_view")) == -1);
  REQUIRE(Fvopt32(nullptr, 0, DECONST("no_such_view")) == -1);
  REQUIRE(Fvneeded32(DECONST("no_such_view")) == -1);

  // null struct arguments
  REQUIRE(Fvsinit32(nullptr, DECONST("v_all")) == -1);
  REQUIRE(Fvftos32(nullptr, cstruct, DECONST("v_all")) == -1);
  REQUIRE(Fvstof32(nullptr, cstruct, 0, DECONST("v_all")) == -1);

  // null names
  REQUIRE(Fvsinit32(cstruct, nullptr) == -1);
  REQUIRE(Fvftos32(nullptr, cstruct, nullptr) == -1);
  REQUIRE(Fvstof32(nullptr, cstruct, 0, nullptr) == -1);

  // NOTE: Fvnull32(cstruct, NULL, 0, <valid view>) segfaults on Oracle
  // 12.2.2 itself (probed); fuxedo returns -1. Not exercised here so
  // the suite stays runnable against both.

  // invalid mode character for Fvstof32
  auto *vf = Falloc32(10, 100);
  REQUIRE(vf != nullptr);
  REQUIRE(Fvstof32(vf, DECONST("field=1"), 'Z', DECONST("v_all")) == -1);
  Ffree32(vf);

  // Fvboolco32 with a view that exists but a null expression
  REQUIRE(Fvboolco32(nullptr, DECONST("v_expr")) == nullptr);
  REQUIRE(Ferror32 == FEINVAL);
}

TEST_CASE("XATMI argument validation", "[hardening]") {
  // buffer management
  REQUIRE(tpalloc(nullptr, nullptr, 100) == nullptr);
  REQUIRE(tperrno == TPEINVAL);
  REQUIRE(tpalloc(DECONST("NO_SUCH_TYPE"), nullptr, 100) == nullptr);
  REQUIRE(tperrno == TPENOENT);
  // negative sizes are clamped to the type default on the platform
  // too (verified against Oracle 12.2), so a buffer comes back
  auto *neg = tpalloc(DECONST("STRING"), nullptr, -1);
  REQUIRE(neg != nullptr);
  tpfree(neg);
  auto *buf = tpalloc(DECONST("STRING"), nullptr, 10);
  REQUIRE(buf != nullptr);
  // the platform leaves tperrno untouched on tpalloc success (probed);
  // the tptypes/tpfree/tpexport/tpimport family resets it
  tperrno = TPEINVAL;
  REQUIRE(tpalloc(DECONST("STRING"), nullptr, 10) != nullptr);
  REQUIRE(tperrno == TPEINVAL);
  tperrno = TPEINVAL;
  REQUIRE(tptypes(buf, nullptr, nullptr) != -1);
  REQUIRE(tperrno == 0);
  tperrno = TPEINVAL;
  REQUIRE(tprealloc(buf, -1) != nullptr);
  REQUIRE(tperrno == 0);
  tpfree(buf);
  tpfree(nullptr);  // must be a harmless no-op
  REQUIRE(tptypes(nullptr, nullptr, nullptr) == -1);

  // export/import round-trip guards beyond the fml32 suite
  long olen = 0;
  REQUIRE(tpexport(nullptr, 0, nullptr, &olen, 0) == -1);
  REQUIRE(tpimport(nullptr, 0, nullptr, &olen, 0) == -1);

  // never crash; the exact code depends on the platform state (fuxedo
  // without a domain answers -1), so only the failure itself is pinned
  char *call_buf = tpalloc(DECONST("STRING"), nullptr, 100);
  REQUIRE(call_buf != nullptr);
  strcpy(call_buf, "hi");
  long rlen = 0;
  REQUIRE(tpcall(DECONST(""), call_buf, 0, &call_buf, &rlen, 0) == -1);
  REQUIRE(tperrno != 0);
  REQUIRE(tpcall(DECONST("no_such_service_anywhere"), call_buf, 0, &call_buf,
                 &rlen, 0) == -1);
  REQUIRE((tperrno == -1 || tperrno == TPENOENT || tperrno == TPELIMIT ||
           tperrno == TPEPROTO || tperrno == TPESYSTEM));

  // over-long names beyond XATMI_SERVICE_NAME_LENGTH
  std::string longname(200, 'X');
  REQUIRE(tpcall(const_cast<char *>(longname.c_str()), call_buf, 0, &call_buf,
                 &rlen, 0) == -1);

  // invalid call flags
  REQUIRE(tpcall(DECONST("no_such_service_anywhere"), call_buf, 0, &call_buf,
                 &rlen, TPNOTRAN | TPSIGRSTRT) == -1);

  // tpacall without waiting: same name validation
  int cd = tpacall(DECONST("no_such_service_anywhere"), call_buf, 0, 0);
  if (cd != -1) {
    // accepted for later failure: the reply must still report an error
    REQUIRE(tpgetrply(&cd, &call_buf, &rlen, 0) == -1);
  }
  tpfree(call_buf);

  // transaction demarcation with no booted application: must not
  // crash. fuxedo with a readable TUXCONFIG allows client transactions
  // (the txq design); Oracle and config-less contexts refuse. Success
  // is != -1 on both platforms (pinned). tpgetlev answers -1 where no
  // RM state exists, 0 otherwise.
  if (tpbegin(30, 0) != -1) {
    REQUIRE(tpabort(0) != -1);
  } else {
    REQUIRE(tpcommit(0) == -1);
    REQUIRE(tpabort(0) == -1);
  }
  REQUIRE((tpgetlev() == 0 || tpgetlev() == -1));

  // administrative call surface (tpadmcall lives in libtmib on the
  // platform and is pinned by the online-updates probes);
  // tpstrerror must leave tperrno alone - code routinely calls it
  // several times while handling an error (pinned: Oracle untouched)
  tperrno = TPEINVAL;
  REQUIRE(tpstrerror(TPELIMIT) != nullptr);
  REQUIRE(tpstrerror(TPEBLOCK) != nullptr);
  REQUIRE(tperrno == TPEINVAL);
}
