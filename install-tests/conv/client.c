#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <userlog.h>

// echo exchange: connect carries the first message, the client drives
// with TPRECVONLY sends, the service echoes and finishes with an empty
// message; the caller watches SENDONLY and SVCSUCC events
static int basic() {
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;

  strcpy(buf, "hello1");
  int cd = tpconnect("ECHO", buf, 0, TPSENDONLY);
  userlog(":TEST: connect cd=%d tperrno=%d", cd, tperrno);
  assert(cd != -1);

  strcpy(buf, "hello2");
  assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);

  assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
  assert(strcmp(buf, "hello1") == 0);
  assert(revent & TPEV_SENDONLY);

  strcpy(buf, "hello3");
  assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
  assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
  assert(strcmp(buf, "hello2") == 0);
  assert(revent & TPEV_SENDONLY);

  strcpy(buf, "");
  assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
  assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
  userlog(":TEST: BASIC final len=%ld revent=0x%lx data=%s", len, revent,
          buf);
  assert(revent & TPEV_SVCSUCC);
  assert(strcmp(buf, "done") == 0);
  assert(tpurcode == 7);

  // the conversation is over
  strcpy(buf, "more");
  assert(tpsend(cd, buf, 0, 0, &revent) == -1);
  assert(tperrno == TPEBADDESC);
  tpfree(buf);
  userlog(":TEST: BASIC OK");
  return 0;
}

// a failing service delivers SVCFAIL with rcode and data
static int fail() {
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;

  strcpy(buf, "ping");
  int cd = tpconnect("FAILSVC", buf, 0, TPSENDONLY);
  assert(cd != -1);
  userlog(":TEST: connect cd=%d tperrno=%d", cd, tperrno);
  assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
  assert(revent & TPEV_SVCFAIL);
  assert(tpurcode == 42);
  assert(strcmp(buf, "oops") == 0);
  tpfree(buf);
  userlog(":TEST: FAIL OK");
  return 0;
}

// client-side tpdiscon: the blocked service wakes with DISCONIMM
static int cdiscon() {
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;

  int cd = tpconnect("SLEEP", buf, 0, TPSENDONLY);
  assert(cd != -1);
  userlog(":TEST: connect cd=%d tperrno=%d", cd, tperrno);
  assert(tpdiscon(cd) != -1);
  assert(tpsend(cd, "y", 0, 0, &revent) == -1);
  assert(tperrno == TPEBADDESC);
  tpfree(buf);
  userlog(":TEST: CDISCON OK");
  return 0;
}

// TPRECVONLY at connect: the service holds send permission first
static int srvfirst() {
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;

  int cd = tpconnect("SENDFIRST", NULL, 0, TPRECVONLY);
  assert(cd != -1);
  userlog(":TEST: connect cd=%d tperrno=%d", cd, tperrno);
  // sending without permission is a protocol error
  assert(tpsend(cd, "early", 0, 0, &revent) == -1);
  assert(tperrno == TPEPROTO);

  assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
  assert(strcmp(buf, "srv-first") == 0);
  assert(revent & TPEV_SENDONLY);

  strcpy(buf, "client-data");
  assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
  assert(tprecv(cd, &buf, &len, 0, &revent) != -1);
  assert(revent & TPEV_SVCSUCC);
  assert(strcmp(buf, "srv-done") == 0);
  tpfree(buf);
  userlog(":TEST: SRVFIRST OK");
  return 0;
}

// unknown service and bad descriptors
static int negative() {
  char *buf = tpalloc("STRING", NULL, 64);
  long revent = 0;
  strcpy(buf, "hi");
  assert(tpconnect("NOSUCHSVC", NULL, 0, TPSENDONLY) == -1);
  assert(tperrno == TPENOENT);
  assert(tpconnect("ECHO", NULL, 0, 0x1000000) == -1);
  assert(tperrno == TPEINVAL);

  int cd = tpconnect("ECHO", buf, 0, TPSENDONLY);
  userlog(":TEST: connect cd=%d tperrno=%d", cd, tperrno);
  assert(cd != -1);
  assert(tpsend(cd + 500000, buf, 0, 0, &revent) == -1);
  assert(tperrno == TPEBADDESC);

  char *small = tpalloc("STRING", NULL, 8);
  long len = 0;
  // the service answers the connect data only after we hand over send
  // permission; drive one round and close cleanly
  strcpy(buf, "x");
  assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
  assert(tprecv(cd, &small, &len, 0, &revent) != -1);
  assert(strcmp(small, "hi") == 0);
  strcpy(buf, "");
  assert(tpsend(cd, buf, 0, TPRECVONLY, &revent) != -1);
  assert(tprecv(cd, &small, &len, 0, &revent) != -1);
  assert(revent & TPEV_SVCSUCC);
  tpfree(small);
  tpfree(buf);
  userlog(":TEST: NEGATIVE OK");
  return 0;
}

// large message: spills from the queue into a /tmp file, only the
// file name travels the queue (the platform's file transfer)
static int large() {
  long n = 200000;
  char *buf = tpalloc("CARRAY", NULL, n);
  assert(buf != NULL);
  for (long i = 0; i < n; i++) {
    buf[i] = (char)(i % 251);
  }
  char *rcv = tpalloc("CARRAY", NULL, n);
  long rlen = 0;
  int rc = tpcall("ECHOBIG", buf, n, &rcv, &rlen, 0);
  userlog(":TEST: LARGE call rc=%d tperrno=%d", rc, tperrno);
  assert(rc != -1);
  assert(rlen == n);
  for (long i = 0; i < n; i++) {
    assert(rcv[i] == (char)(i % 251));
  }
  userlog(":TEST: LARGE OK len=%ld", rlen);
  return 0;
}

int main(int argc, char *argv[]) {
  if (tpinit(NULL) == -1) {
    fprintf(stderr, "tpinit failed %d\n", tperrno);
    return 1;
  }
  int rc = 0;
  if (argc > 1 && strcmp(argv[1], "basic") == 0) {
    rc = basic();
  } else if (argc > 1 && strcmp(argv[1], "fail") == 0) {
    rc = fail();
  } else if (argc > 1 && strcmp(argv[1], "cdiscon") == 0) {
    rc = cdiscon();
  } else if (argc > 1 && strcmp(argv[1], "srvfirst") == 0) {
    rc = srvfirst();
  } else if (argc > 1 && strcmp(argv[1], "negative") == 0) {
    rc = negative();
  } else if (argc > 1 && strcmp(argv[1], "large") == 0) {
    rc = large();
  } else {
    rc = 1;
  }
  tpterm();
  return rc;
}
