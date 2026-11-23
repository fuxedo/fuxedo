#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <userlog.h>
#include <unistd.h>

// echoes every message back with TPRECVONLY until an empty message,
// then returns success; the connect data arrives as svcinfo->data and
// is answered at the first opportunity, once the caller has handed
// over send permission (the exchange is half-duplex, like the platform)
void ECHO(TPSVCINFO *svcinfo) {
  userlog(":TEST: ECHO called flags=0x%lx first=%s", svcinfo->flags,
          svcinfo->data);
  assert(svcinfo->flags & TPCONV);
  char *pending = tpalloc("STRING", NULL, 1024);
  strcpy(pending, svcinfo->data);
  char *donebuf = tpalloc("STRING", NULL, 16);
  strcpy(donebuf, "done");
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;
  while (1) {
    int rc = tprecv(svcinfo->cd, &buf, &len, 0, &revent);
    // events answer -1/TPEEVENT with the data delivered (pinned on
    // Oracle 22.1); a real failure keeps a different tperrno
    if (rc == -1 && tperrno != TPEEVENT) {
      userlog(":TEST: ECHO tprecv failed %d", tperrno);
      break;
    }
    userlog(":TEST: ECHO recv len=%ld revent=0x%lx data=%s", len, revent, buf);
    if (revent & TPEV_DISCONIMM) {
      break;
    }
    if (len <= 1) {
      // the empty message ends the exchange
      tpreturn(TPSUCCESS, 7, donebuf, 0, 0);
    }
    if (tpsend(svcinfo->cd, pending, 0, TPRECVONLY, &revent) == -1) {
      userlog(":TEST: ECHO tpsend failed %d", tperrno);
      break;
    }
    strcpy(pending, buf);
  }
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}

// receives one message and fails with an rcode the caller checks
void FAILSVC(TPSVCINFO *svcinfo) {
  userlog(":TEST: FAILSVC called data=%s", svcinfo->data);
  assert(strcmp(svcinfo->data, "ping") == 0);
  char *b = tpalloc("STRING", NULL, 16);
  strcpy(b, "oops");
  tpreturn(TPFAIL, 42, b, 0, 0);
}

// blocks in recv so the client can tpdiscon underneath it; the
// disconnect may arrive in the first or the second recv
void SLEEP(TPSVCINFO *svcinfo) {
  userlog(":TEST: SLEEP called");
  char *done = tpalloc("STRING", NULL, 16);
  strcpy(done, "slept");
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;
  int rc = tprecv(svcinfo->cd, &buf, &len, 0, &revent);
  if (rc == -1 || (revent & TPEV_DISCONIMM)) {
    userlog(":TEST: SLEEP disconnect in first recv rc=%d revent=0x%lx", rc,
            revent);
    tpreturn(TPSUCCESS, 0, done, 0, 0);
  }
  userlog(":TEST: SLEEP first recv done, waiting for the disconnect");
  rc = tprecv(svcinfo->cd, &buf, &len, 0, &revent);
  userlog(":TEST: SLEEP second recv rc=%d revent=0x%lx", rc, revent);
  assert(rc == -1);
  assert(tperrno == TPEEVENT);
  assert(revent & TPEV_DISCONIMM);
  tpreturn(TPSUCCESS, 0, done, 0, 0);
}

// replies with a big buffer so the caller's request and the reply
// both take the file-transfer path through the queue
void ECHOBIG(TPSVCINFO *svcinfo) {
  userlog(":TEST: ECHOBIG called len=%ld", svcinfo->len);
  assert(svcinfo->len == 200000);
  long n = 200000;
  char *buf = tpalloc("CARRAY", NULL, n);
  for (long i = 0; i < n; i++) {
    buf[i] = (char)(i % 251);
  }
  tpreturn(TPSUCCESS, 5, buf, n, 0);
}

// the client connected with TPRECVONLY, so this side sends first
void SENDFIRST(TPSVCINFO *svcinfo) {
  userlog(":TEST: SENDFIRST called");
  char *first = tpalloc("STRING", NULL, 16);
  strcpy(first, "srv-first");
  char *sdone = tpalloc("STRING", NULL, 16);
  strcpy(sdone, "srv-done");
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;
  assert(tpsend(svcinfo->cd, first, 0, TPRECVONLY, &revent) != -1);
  int rc = tprecv(svcinfo->cd, &buf, &len, 0, &revent);
  // the client's data rides with the SENDONLY event: -1/TPEEVENT
  assert(rc == -1);
  assert(tperrno == TPEEVENT);
  userlog(":TEST: SENDFIRST recv data=%s revent=0x%lx", buf, revent);
  assert(strcmp(buf, "client-data") == 0);
  tpreturn(TPSUCCESS, 9, sdone, 0, 0);
}
