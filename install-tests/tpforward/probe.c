// behavior probe - prints observed outcomes instead of asserting, used
// to pin Oracle's semantics before the fixture asserts them
#include <atmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static char *sndbuf;
static char *rcvbuf;

static void call(const char *name) {
  long rcvlen = 6;
  int ret = tpcall((char *)name, sndbuf, 0, &rcvbuf, &rcvlen, 0);
  printf("PROBE %s: ret=%d tperrno=%d tpurcode=%ld data=%s\n", name, ret,
         tperrno, tpurcode, ret == 0 ? rcvbuf : "-");
  fflush(stdout);
}

int main() {
  sndbuf = tpalloc("STRING", NULL, 6);
  strcpy(sndbuf, "HELLO");
  rcvbuf = tpalloc("STRING", NULL, 6);

  call("TXFWDFAIL");

  int rc = tpbegin(30, 0);
  printf("PROBE tpbegin: rc=%d tperrno=%d lev=%d\n", rc, tperrno, tpgetlev());
  call("FWDTX");
  rc = tpcommit(0);
  printf("PROBE tpcommit after FWDTX: rc=%d tperrno=%d lev=%d\n", rc, tperrno,
         tpgetlev());

  rc = tpbegin(30, 0);
  printf("PROBE tpbegin2: rc=%d tperrno=%d lev=%d\n", rc, tperrno, tpgetlev());
  call("FWDNOTRAN");
  rc = tpcommit(0);
  printf("PROBE tpcommit after FWDNOTRAN: rc=%d tperrno=%d lev=%d\n", rc,
         tperrno, tpgetlev());

  rc = tpbegin(30, 0);
  printf("PROBE tpbegin3: rc=%d tperrno=%d lev=%d\n", rc, tperrno, tpgetlev());
  rc = tpabort(0);
  printf("PROBE tpabort: rc=%d tperrno=%d lev=%d\n", rc, tperrno, tpgetlev());

  // TPNOREPLY: if the caller blocks forever, the alarm kills us - that
  // is a result too
  alarm(20);
  call("FWDNOREPLY");
  alarm(0);

  // timing: released at forward time (fast) or at target return (slow)?
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  alarm(20);
  call("FWDNOSLOW");
  alarm(0);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  printf("PROBE FWDNOSLOW elapsed: %.1fs\n",
         (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
  fflush(stdout);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
