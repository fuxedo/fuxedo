// Flag validation matrix for the tp* entry points, run against a
// booted domain (BBL only, no application servers) so every call
// reaches flag validation with a live session. Expected values are
// pinned against Oracle Tuxedo 12.2.2 (probes tmp/flagprobe/mask*).
// Both platforms must produce the identical matrix.

#include <atmi.h>
#include <userlog.h>

#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect_reject(const char *what, long flags) {
  char *buf = tpalloc("STRING", NULL, 100);
  strcpy(buf, "hi");
  long rlen = 100;
  int rc = tpcall("NO_SVC", buf, 0, &buf, &rlen, flags);
  if (rc != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL %s flags=0x%lx rc=%d tperrno=%d, want -1/TPEINVAL",
            what, flags, rc, tperrno);
    g_fail++;
  }
  tpfree(buf);
}

static void expect_accept_tpENOENT(const char *what, long flags) {
  char *buf = tpalloc("STRING", NULL, 100);
  strcpy(buf, "hi");
  long rlen = 100;
  int rc = tpcall("NO_SVC", buf, 0, &buf, &rlen, flags);
  if (!(rc == -1 && tperrno == TPENOENT)) {
    userlog(":TEST: FAIL %s flags=0x%lx rc=%d tperrno=%d, want -1/TPENOENT",
            what, flags, rc, tperrno);
    g_fail++;
  }
  tpfree(buf);
}

// tpacall to a missing service: accepted flags yield a descriptor whose
// getrply fails TPENOENT (or, with TPNOREPLY, a bare 0)
static void expect_accept_acall(const char *what, long flags) {
  char *buf = tpalloc("STRING", NULL, 100);
  strcpy(buf, "hi");
  int cd = tpacall("NO_SVC", buf, 0, flags);
  int bad = 0;
  if (cd == -1) {
    bad = (tperrno == TPEINVAL);
  } else if (flags & TPNOREPLY) {
    bad = 0;
  } else {
    long rlen = 100;
    char *rbuf = tpalloc("STRING", NULL, 100);
    int rc = tpgetrply(&cd, &rbuf, &rlen, 0);
    bad = (rc == -1 && tperrno == TPEINVAL);
    tpfree(rbuf);
  }
  if (bad) {
    userlog(":TEST: FAIL tpacall %s flags=0x%lx tperrno=%d, want accepted",
            what, flags, tperrno);
    g_fail++;
  }
  tpfree(buf);
}

static void expect_reject_acall(const char *what, long flags) {
  char *buf = tpalloc("STRING", NULL, 100);
  strcpy(buf, "hi");
  int cd = tpacall("NO_SVC", buf, 0, flags);
  int rc = cd;
  if (cd != -1 && !(flags & TPNOREPLY)) {
    long rlen = 100;
    rc = tpgetrply(&cd, &buf, &rlen, 0);
  }
  if (rc != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpacall %s flags=0x%lx rc=%d tperrno=%d, want "
            "-1/TPEINVAL",
            what, flags, rc, tperrno);
    g_fail++;
  }
  tpfree(buf);
}

static void tpcall_matrix() {
  expect_accept_tpENOENT("tpcall NOBLOCK", TPNOBLOCK);
  expect_accept_tpENOENT("tpcall SIGRSTRT", TPSIGRSTRT);
  expect_accept_tpENOENT("tpcall NOTRAN", TPNOTRAN);
  expect_accept_tpENOENT("tpcall NOTIME", TPNOTIME);
  expect_accept_tpENOENT("tpcall NOCHANGE", TPNOCHANGE);
  expect_accept_tpENOENT("tpcall NOCOPY", TPNOCOPY);
  expect_reject("tpcall NOREPLY", TPNOREPLY);
  expect_reject("tpcall TRAN", TPTRAN);
  expect_reject("tpcall GETANY", TPGETANY);
  expect_reject("tpcall ABSOLUTE", TPABSOLUTE);
  expect_reject("tpcall CONV", TPCONV);
  expect_reject("tpcall SENDONLY", TPSENDONLY);
  expect_reject("tpcall RECVONLY", TPRECVONLY);
  expect_reject("tpcall PACK", TPACK);
  expect_reject("tpcall bit14", 0x4000);
  expect_reject("tpcall all", -1);
  userlog(":TEST: tpcall matrix done");
}

static void tpacall_matrix() {
  expect_accept_acall("NOBLOCK", TPNOBLOCK);
  expect_accept_acall("SIGRSTRT", TPSIGRSTRT);
  expect_accept_acall("NOREPLY", TPNOREPLY);
  expect_accept_acall("NOTRAN", TPNOTRAN);
  expect_accept_acall("NOTIME", TPNOTIME);
  expect_reject_acall("TRAN", TPTRAN);
  expect_reject_acall("NOCHANGE", TPNOCHANGE);
  expect_reject_acall("NOCOPY", TPNOCOPY);
  expect_reject_acall("GETANY", TPGETANY);
  expect_reject_acall("CONV", TPCONV);
  expect_reject_acall("SENDONLY", TPSENDONLY);
  expect_reject_acall("RECVONLY", TPRECVONLY);
  expect_reject_acall("bit14", 0x4000);
  expect_reject_acall("all", -1);
  userlog(":TEST: tpacall matrix done");
}

static void tpgetrply_matrix() {
  char *buf = tpalloc("STRING", NULL, 100);
  long rlen = 100;
  int cd;
  int fails0 = g_fail;

  // valid single bits against a never-issued descriptor: bad descriptor
  long valid[] = {TPNOBLOCK, TPSIGRSTRT, TPNOTIME, TPNOCHANGE};
  for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
    cd = -1;
    if (tpgetrply(&cd, &buf, &rlen, valid[i]) != -1 ||
        tperrno != TPEBADDESC) {
      userlog(":TEST: FAIL tpgetrply flags=0x%lx tperrno=%d, want "
              "-1/TPEBADDESC",
              valid[i], tperrno);
      g_fail++;
    }
  }
  // TPGETANY with nothing outstanding: protocol error, not a wait
  cd = -1;
  if (tpgetrply(&cd, &buf, &rlen, TPGETANY | TPNOBLOCK) != -1 ||
      tperrno != TPEPROTO) {
    userlog(":TEST: FAIL tpgetrply GETANY|NOBLOCK tperrno=%d, want "
            "-1/TPEPROTO",
            tperrno);
    g_fail++;
  }

  long bad[] = {TPNOREPLY, TPNOTRAN, TPTRAN, TPABSOLUTE, TPCONV,
                TPSENDONLY, TPRECVONLY, TPACK, TPNOCOPY, 0x4000, -1};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    cd = -1;
    if (tpgetrply(&cd, &buf, &rlen, bad[i]) != -1 ||
        tperrno != TPEINVAL) {
      userlog(":TEST: FAIL tpgetrply flags=0x%lx tperrno=%d, want "
              "-1/TPEINVAL",
              bad[i], tperrno);
      g_fail++;
    }
  }
  if (g_fail == fails0) {
    userlog(":TEST: tpgetrply matrix done");
  }
  tpfree(buf);
}

static void conv_matrix() {
  char *buf = tpalloc("STRING", NULL, 100);
  strcpy(buf, "hi");
  long ev = 0, rev = 0, rlen = 100;
  int fails0 = g_fail;

  // tpconnect: ONLY an explicit direction is accepted; flags=0 is
  // rejected outright (pinned 12.2.2 - full duplex must be spelled out
  // with TPSENDONLY|TPRECVONLY)
  if (tpconnect("NO_SVC", buf, 0, 0) != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpconnect(0) tperrno=%d, want -1/TPEINVAL",
            tperrno);
    g_fail++;
  }
  if (tpconnect("NO_SVC", buf, 0, TPSENDONLY | TPRECVONLY) != -1 ||
      tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpconnect(BOTH) tperrno=%d, want -1/TPEINVAL",
            tperrno);
    g_fail++;
  }
  if (tpconnect("NO_SVC", buf, 0, TPSENDONLY) != -1 ||
      tperrno != TPENOENT) {
    userlog(":TEST: FAIL tpconnect(SENDONLY) tperrno=%d, want -1/TPENOENT",
            tperrno);
    g_fail++;
  }
  if (tpconnect("NO_SVC", buf, 0, TPRECVONLY) != -1 ||
      tperrno != TPENOENT) {
    userlog(":TEST: FAIL tpconnect(RECVONLY) tperrno=%d, want -1/TPENOENT",
            tperrno);
    g_fail++;
  }
  long badconn[] = {TPNOBLOCK, TPSIGRSTRT, TPNOTRAN, TPNOTIME,
                    TPNOREPLY, TPGETANY,  0x4000,   -1};
  for (unsigned i = 0; i < sizeof(badconn) / sizeof(badconn[0]); i++) {
    if (tpconnect("NO_SVC", buf, 0, badconn[i]) != -1 ||
        tperrno != TPEINVAL) {
      userlog(":TEST: FAIL tpconnect flags=0x%lx tperrno=%d, want "
              "-1/TPEINVAL",
              badconn[i], tperrno);
      g_fail++;
    }
  }

  // tpsend/tprecv against a never-issued descriptor
  long svalid[] = {0, TPNOBLOCK, TPSIGRSTRT, TPNOTIME, TPRECVONLY};
  for (unsigned i = 0; i < sizeof(svalid) / sizeof(svalid[0]); i++) {
    if (tpsend(-1, buf, 3, svalid[i], &rev) != -1 ||
        tperrno != TPEBADDESC) {
      userlog(":TEST: FAIL tpsend flags=0x%lx tperrno=%d, want "
              "-1/TPEBADDESC",
              svalid[i], tperrno);
      g_fail++;
    }
  }
  long sbad[] = {TPSENDONLY, TPNOREPLY, TPNOTRAN, TPTRAN,
                 TPGETANY,   TPNOCHANGE, TPACK,   TPNOCOPY, 0x4000};
  for (unsigned i = 0; i < sizeof(sbad) / sizeof(sbad[0]); i++) {
    if (tpsend(-1, buf, 3, sbad[i], &rev) != -1 || tperrno != TPEINVAL) {
      userlog(":TEST: FAIL tpsend flags=0x%lx tperrno=%d, want -1/TPEINVAL",
              sbad[i], tperrno);
      g_fail++;
    }
  }
  long rvalid[] = {0, TPNOBLOCK, TPSIGRSTRT, TPNOTIME, TPNOCHANGE};
  for (unsigned i = 0; i < sizeof(rvalid) / sizeof(rvalid[0]); i++) {
    if (tprecv(-1, &buf, &rlen, rvalid[i], &ev) != -1 ||
        tperrno != TPEBADDESC) {
      userlog(":TEST: FAIL tprecv flags=0x%lx tperrno=%d, want "
              "-1/TPEBADDESC",
              rvalid[i], tperrno);
      g_fail++;
    }
  }
  long rbad[] = {TPACK,       TPSENDONLY, TPRECVONLY, TPNOREPLY, TPNOTRAN,
                 TPTRAN,      TPGETANY,   TPABSOLUTE, TPNOCOPY,  0x4000};
  for (unsigned i = 0; i < sizeof(rbad) / sizeof(rbad[0]); i++) {
    if (tprecv(-1, &buf, &rlen, rbad[i], &ev) != -1 ||
        tperrno != TPEINVAL) {
      userlog(":TEST: FAIL tprecv flags=0x%lx tperrno=%d, want -1/TPEINVAL",
              rbad[i], tperrno);
      g_fail++;
    }
  }
  if (g_fail == fails0) {
    userlog(":TEST: conv matrix done");
  }
  tpfree(buf);
}

static void demarcation_matrix() {
  int fails0 = g_fail;  if (tpbegin(30, 0) != 1) {
    userlog(":TEST: FAIL tpbegin(30,0) rc tperrno=%d, want 1", tperrno);
    g_fail++;
  } else {
    tpabort(0);
  }
  if (tpbegin(30, TPNOBLOCK) != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpbegin flags tperrno=%d, want -1/TPEINVAL",
            tperrno);
    g_fail++;
  }
  if (tpcommit(TPNOBLOCK) != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpcommit flags tperrno=%d, want -1/TPEINVAL",
            tperrno);
    g_fail++;
  }
  if (tpabort(TPSIGRSTRT) != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpabort flags tperrno=%d, want -1/TPEINVAL",
            tperrno);
    g_fail++;
  }
  if (g_fail == fails0) {
    userlog(":TEST: demarcation matrix done");
  }
}

static void priority_matrix() {
  int fails0 = g_fail;
  // pinned against Oracle 12.2.2 (tmp/flagprobe/prio): the flag word
  // is validated (anything but 0/TPABSOLUTE is TPEINVAL), any priority
  // value is accepted, and the value has no scheduling effect.
  // Oracle's client-side tpgprio() reads -1 regardless, so the
  // read-back is fuxedo-side, gated at runtime.
  if (tpsprio(1, TPNOBLOCK) != -1 || tperrno != TPEINVAL) {
    userlog(":TEST: FAIL tpsprio bad flags tperrno=%d", tperrno);
    g_fail++;
  }
  if (tpsprio(0, TPABSOLUTE) == -1) {
    userlog(":TEST: FAIL tpsprio(0) rejected");
    g_fail++;
  }
  if (tpsprio(101, TPABSOLUTE) == -1) {
    userlog(":TEST: FAIL tpsprio(101) rejected");
    g_fail++;
  }
  if (tpsprio(-7, TPABSOLUTE) == -1) {
    userlog(":TEST: FAIL tpsprio(-7) rejected");
    g_fail++;
  }
  int now = tpgprio();
  if (now != -1 && now != -7) {
    userlog(":TEST: FAIL tpgprio read-back got %d", now);
    g_fail++;
  }
  // the priority attribute rides the call: tpgprio() inside the
  // service returns the caller's value (pinned on both platforms);
  // an unset caller reads the documented 50
  char *pb = tpalloc("STRING", NULL, 8);
  strcpy(pb, "x");
  long pl = 8;
  tpsprio(77, TPABSOLUTE);
  if (tpcall("PRIO", pb, 0, &pb, &pl, 0) == -1) {
    userlog(":TEST: FAIL PRIO call %d", tperrno);
    g_fail++;
  }
  // one-shot semantics (pinned live): the value applies to the NEXT
  // call only, so a fresh call without tpsprio reads the 50 default
  // in the service
  pl = 8;
  if (tpcall("PRIO", pb, 0, &pb, &pl, 0) == -1) {
    userlog(":TEST: FAIL PRIO reset call %d", tperrno);
    g_fail++;
  }
  // an explicit value overrides the default for its call
  tpsprio(60, TPABSOLUTE);
  pl = 8;
  if (tpcall("PRIO", pb, 0, &pb, &pl, 0) == -1) {
    userlog(":TEST: FAIL PRIO default call %d", tperrno);
    g_fail++;
  }
  if (g_fail == fails0) {
    userlog(":TEST: priority matrix done");
  }
}

int main() {
  if (tpinit(NULL) == -1) {
    fprintf(stderr, "tpinit failed %d\n", tperrno);
    return 1;
  }
  tpcall_matrix();
  tpacall_matrix();
  tpgetrply_matrix();
  conv_matrix();
  demarcation_matrix();
  priority_matrix();
  tpterm();
  if (g_fail == 0) {
    userlog(":TEST: FLAGMATRIX OK");
    return 0;
  }
  userlog(":TEST: FLAGMATRIX %d FAILURES", g_fail);
  return 1;
}
