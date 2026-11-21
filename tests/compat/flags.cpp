// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// Dead-context flag probes: every tp* entry point must survive flag
// words of any shape without a session and without crashing. The
// STRICT flag matrix (accepted/rejected bits per function, pinned
// against Oracle 12.2.2 on a booted domain) lives in
// install-tests/flags/client.c, which runs against fuxedo and Oracle
// in CI; per-bit tperrno assertions are only sound there, because both
// platforms order flag validation against context checks differently
// depending on bulletin board state (probed, notes/flags-investigation).

#include <catch.hpp>

#include <xatmi.h>
#include <cstring>

#define DECONST(x) const_cast<char *>(x)

namespace {

struct outcome {
  int rc;
  int err;
};

outcome tpcall_flags(long flags) {
  char *buf = tpalloc(DECONST("STRING"), nullptr, 100);
  REQUIRE(buf != nullptr);
  strcpy(buf, "hi");
  long rlen = 0;
  outcome o{tpcall(DECONST("no_such_service_anywhere"), buf, 0, &buf, &rlen,
                   flags),
            tperrno};
  tpfree(buf);
  return o;
}

outcome tpacall_flags(long flags) {
  char *buf = tpalloc(DECONST("STRING"), nullptr, 100);
  REQUIRE(buf != nullptr);
  strcpy(buf, "hi");
  int cd = tpacall(DECONST("no_such_service_anywhere"), buf, 0, flags);
  outcome o{cd, tperrno};
  if (cd != -1 && !(flags & TPNOREPLY)) {
    // an accepted call to a missing service fails at tpgetrply on the
    // platform; drain it so nothing lingers
    char *rbuf = tpalloc(DECONST("STRING"), nullptr, 100);
    long rlen = 0;
    o.rc = tpgetrply(&cd, &rbuf, &rlen, 0);
    o.err = tperrno;
    tpfree(rbuf);
  }
  tpfree(buf);
  return o;
}

// the call failed without the flags being the cause, or succeeded
bool survived(const outcome &o) {
  return o.rc != 0;
}

// reserved bits not assigned to any flag in atmidefs.h
constexpr long UNKNOWN1 = 0x00004000;
constexpr long UNKNOWN2 = 0x00008000 != TPNOCOPY ? 0x00008000 : 0x00020000;
constexpr long UNKNOWN3 = 0x10000000;

}  // namespace

TEST_CASE("tp* calls survive arbitrary flag words without a session",
          "[flags]") {
  // no bulletin board here: both platforms fail these calls on context
  // (Oracle probed: TPESYSTEM everywhere except tpcall, which leaves
  // tperrno untouched; fuxedo: TPESYSTEM via the exception boundary).
  // the only portable assertions are: no crash, no success.
  constexpr long words[] = {
      0,
      TPNOBLOCK,
      TPSIGRSTRT,
      TPNOREPLY,
      TPNOTRAN,
      TPTRAN,
      TPNOTIME,
      TPGETANY,
      TPNOCHANGE,
      TPCONV,
      TPSENDONLY,
      TPRECVONLY,
      TPACK,
      UNKNOWN1,
      UNKNOWN2,
      UNKNOWN3,
      -1,
  };
  for (auto w : words) {
    auto o = tpcall_flags(w);
    REQUIRE(survived(o));
    o = tpacall_flags(w);
    REQUIRE(survived(o));
  }

  char *buf = tpalloc(DECONST("STRING"), nullptr, 100);
  REQUIRE(buf != nullptr);
  long rlen = 0;
  long ev = 0;
  long rev = 0;
  for (auto w : words) {
    int cd = -1;
    REQUIRE(tpgetrply(&cd, &buf, &rlen, w) == -1);
    REQUIRE(tpsend(-1, buf, 0, w, &rev) == -1);
    REQUIRE(tprecv(-1, &buf, &rlen, w, &ev) == -1);
    REQUIRE(tpconnect(DECONST("no_such_service_anywhere"), buf, 0, w) == -1);
  }

  // demarcation: any flag word is rejected; the exact code depends on
  // context (live domain pinned: TPEINVAL, install-tests/flags)
  REQUIRE(tpbegin(30, TPNOBLOCK) == -1);
  REQUIRE(tpcommit(TPNOBLOCK) == -1);
  REQUIRE(tpabort(TPSIGRSTRT) == -1);
  REQUIRE(tpsuspend(nullptr, TPNOBLOCK) == -1);
  REQUIRE(tpresume(nullptr, TPNOBLOCK) == -1);
  tpfree(buf);
}

TEST_CASE("event calls survive arbitrary flag words", "[flags]") {
  char *buf = tpalloc(DECONST("STRING"), nullptr, 100);
  REQUIRE(buf != nullptr);
  strcpy(buf, "hi");

  // the platform IGNORES the flag word on posting, notification, and
  // broadcast (pinned against Oracle 12.2.2 on a booted domain); with
  // no domain both platforms fail on context. Either way: never a
  // flag-driven TPEINVAL, never a crash.
  REQUIRE((tppost(DECONST("NO_SUCH_EVENT"), buf, 0, TPNOBLOCK) == 0 ||
           tperrno != TPEINVAL));
  CLIENTID cid = {};
  tpnotify(&cid, buf, 0, TPNOBLOCK);
  REQUIRE(tperrno != TPEINVAL);
  tpbroadcast(nullptr, nullptr, nullptr, buf, 0, TPNOBLOCK);
  REQUIRE(tperrno != TPEINVAL);

  // tpsubscribe rejects a bare TPEVPERSIST (no delivery class) and
  // unknown bits; the rejections are only observable with a live event
  // server - with a loaded-but-dead domain Oracle answers the context
  // error first (probed), so the strict pins live in the eventbr greps
  const bool ev_live = (tppost(DECONST("FLAGS_LIVE_PROBE"), buf, 3, 0) == 0);
  if (ev_live) {
    REQUIRE(tpsubscribe(DECONST("NO_SUCH_EVENT"), nullptr, nullptr,
                        TPEVPERSIST) == -1);
    REQUIRE(tperrno == TPEINVAL);
    REQUIRE(tpsubscribe(DECONST("NO_SUCH_EVENT"), nullptr, nullptr,
                        TPTRAN) == -1);
    REQUIRE(tperrno == TPEINVAL);
  }

  // valid class selections pass validation; what happens next is
  // context (subscription handle live, context error dead).
  // TPNOBLOCK is intentionally not used as the bad bit: it aliases
  // TPEVSERVICE at the bit level.
  REQUIRE((tpsubscribe(DECONST("NO_SUCH_EVENT"), nullptr, nullptr,
                       TPEVSERVICE) != -2));
  REQUIRE((tpsubscribe(DECONST("NO_SUCH_EVENT"), nullptr, nullptr,
                       TPEVQUEUE) != -2));
  REQUIRE((tpsubscribe(DECONST("NO_SUCH_EVENT"), nullptr, nullptr,
                       TPEVSERVICE | TPEVQUEUE) != -2));

  // tpsetunsol takes a handler, no flags; without a session the
  // platform-style answer is nullptr + an error, never a throw
  auto *a = reinterpret_cast<void (*)(char *, long, long)>(0x1);
  auto *b = reinterpret_cast<void (*)(char *, long, long)>(0x3);
  tpsetunsol(a);
  tpsetunsol(b);
  tpsetunsol(nullptr);
  tpfree(buf);
}
