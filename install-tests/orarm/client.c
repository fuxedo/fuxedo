// Oracle XA Phase 1 driver: exercises the seven single-RM scenarios
// from the multi-RM matrix. Visibility of each row is asserted by the
// Makefile from sqlplus after the run; this program prints the call
// outcomes only.

#include <atmi.h>
#include <fml32.h>
#include <stdio.h>
#include <string.h>

#include "orarm.h"

static FBFR32 *fb;
static char *reply;
static long rlen;

static void svc(const char *tag, long id, const char *value) {
  Finit32(fb, 4096);
  Fchg32(fb, FLD_ID, 0, (char *)&id, 0);
  Fchg32(fb, FLD_TAG, 0, (char *)value, 0);
  int rc = tpcall((char *)tag, (char *)fb, 0, &reply, &rlen, 0);
  printf("%s id=%ld rc=%d tperrno=%d urcode=%ld\n", tag, id, rc, tperrno,
         tpurcode);
}

int main(void) {
  tpinit(NULL);
  fb = (FBFR32 *)tpalloc("FML32", NULL, 4096);
  reply = tpalloc("STRING", NULL, 256);

  // S1: begin, insert, commit -> row visible
  tpbegin(30, 0);
  svc("INSERT", 101, "s1");
  printf("S1 commit rc=%d tperrno=%d\n", tpcommit(0), tperrno);

  // S2: begin, insert, abort -> no row
  tpbegin(30, 0);
  svc("INSERT", 102, "s2");
  printf("S2 abort rc=%d tperrno=%d\n", tpabort(0), tperrno);

  // S3: AUTOTRAN service, TPSUCCESS -> row
  svc("INSERT", 103, "s3");

  // S4: AUTOTRAN service, TPFAIL -> no row, caller sees the failure
  svc("INSERTFAIL", 104, "s4");

  // S5: server-begun transaction committed by the platform at return
  svc("INSERTOWN", 105, "s5");

  // S6: one-second transaction around a two-second insert -> abort
  tpbegin(1, 0);
  svc("INSERT", 106, "slow");
  printf("S6 commit rc=%d tperrno=%d\n", tpcommit(0), tperrno);

  tpfree((char *)fb);
  tpfree(reply);
  tpterm();
  return 0;
}
