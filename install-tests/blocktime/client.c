#include <assert.h>
#include <atmi.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// a caught SIGTERM mid-wait must end the call with TPGOTSIG (the
// platform behavior, pinned against Oracle 12.2.2); the Makefile
// sends the signal a couple of seconds into the SLEEP call
static void on_term(int sig) {
  (void)sig;
}

static int gotsig_mode() {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = on_term;
  assert(sigaction(SIGTERM, &act, NULL) == 0);

  char *buf = tpalloc("STRING", NULL, 6);
  strcpy(buf, "HELLO");
  long rlen = 6;
  int rc = tpcall("SLEEP", buf, 0, &buf, &rlen, 0);
  int err = tperrno;
  printf("gotsig: tpcall rc=%d tperrno=%d\n", rc, err);
  fflush(stdout);
  return rc == -1 && err == TPGOTSIG ? 0 : 1;
}

int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "gotsig") == 0) {
    return gotsig_mode();
  }
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;
  int cd = tpacall("SLEEP", sndbuf, 0, 0);
  assert(cd > 0);

  time_t start;

  start = time(NULL);

  assert(tpsblktime(3, TPBLK_ALL) != -1);
  assert(tpsblktime(1, TPBLK_NEXT) != -1);
  int ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETIME);
  assert(time(NULL) - start >= 1);
  assert(time(NULL) - start < 3);

  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETIME);
  assert(time(NULL) - start >= 3);

  sleep(6);

  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);

  cd = tpacall("SLEEP", sndbuf, 0, 0);
  assert(cd > 0);

  // reply-side timeout semantics while the server sleeps. Queue-full
  // SEND behavior (TPEBLOCK, spill promotion) moved to the
  // deterministic hisstrace chain in pytests/test_strace.py - here the
  // server queue never fills, and relying on incidental accumulation
  // broke with the platform's file-transfer promotion.
  cd = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd > 0);
  start = time(NULL);
  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETIME);
  assert(time(NULL) - start >= 3);
  tpcancel(cd);

  cd = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd > 0);
  // TPBLK_NEXT is consumed by the send's blocktime lookup; set it for
  // the reply wait specifically
  assert(tpsblktime(1, TPBLK_NEXT) != -1);
  start = time(NULL);
  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETIME);
  assert(time(NULL) - start >= 1);
  assert(time(NULL) - start < 3);
  tpcancel(cd);

  // outlive the pending SLEEP so the queued SERVICE requests run; then
  // a plain call must succeed against the free server
  sleep(12);
  cd = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd > 0);
  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);
  assert(strcmp(rcvbuf, "HELLO") == 0);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
