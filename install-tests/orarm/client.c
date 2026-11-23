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

  // S7: two-RM commit: Oracle insert + /Q enqueue in one transaction
  tpbegin(30, 0);
  svc("INSERT", 201, "s7");
  {
    char *qmsg = tpalloc("STRING", NULL, 32);
    strcpy(qmsg, "two-rm-commit");
    TPQCTL qctl;
    memset(&qctl, 0, sizeof(qctl));
    int rc = tpenqueue("QSPACE", "RAWQ", &qctl, qmsg, 0, 0);
    printf("S7 enqueue rc=%d tperrno=%d\n", rc, tperrno);
    tpfree(qmsg);
  }
  printf("S7 commit rc=%d tperrno=%d\n", tpcommit(0), tperrno);

  // S8: two-RM abort: neither the row nor the message may survive
  tpbegin(30, 0);
  svc("INSERT", 202, "s8");
  {
    char *qmsg = tpalloc("STRING", NULL, 32);
    strcpy(qmsg, "two-rm-abort");
    TPQCTL qctl;
    memset(&qctl, 0, sizeof(qctl));
    int rc = tpenqueue("QSPACE", "RAWQ", &qctl, qmsg, 0, 0);
    printf("S8 enqueue rc=%d tperrno=%d\n", rc, tperrno);
    tpfree(qmsg);
  }
  printf("S8 abort rc=%d tperrno=%d\n", tpabort(0), tperrno);

  // post-run probe: the S7 message must be dequeuable, S8's never
  {
    char *qmsg = tpalloc("STRING", NULL, 64);
    TPQCTL qctl;
    memset(&qctl, 0, sizeof(qctl));
    long qlen = 64;
    int rc = tpdequeue("QSPACE", "RAWQ", &qctl, &qmsg, &qlen, 0);
    printf("S7 probe dequeue rc=%d tperrno=%d msg=%s\n", rc, tperrno,
           rc == 0 ? qmsg : "-");
    tpfree(qmsg);
  }

  // S9: two-RM queue pipeline. SUBMIT inserts the row (Oracle RM) and
  // enqueues its id (/Q RM) in one AUTOTRAN transaction; TMQFORWARD
  // forwards each message to the JOBS service inside another
  // transaction, which reads the row and marks it processed. The
  // qretry batch fails its first delivery on purpose: a failed forward
  // aborts the dequeue, so the message is redelivered - the same
  // mechanism that covers /Q becoming visible before Oracle's commit.
  for (long i = 0; i < 5; i++) {
    svc("SUBMIT", 301 + i, "q");
  }
  for (long i = 0; i < 3; i++) {
    svc("SUBMIT", 311 + i, "qretry");
  }

  // an empty JOBS queue means every forward committed, and each row
  // update committed in the same transaction as its dequeue
  {
    char *qmsg = tpalloc("STRING", NULL, 64);
    int drained = 0;
    for (int tries = 0; tries < 90; tries++) {
      TPQCTL qctl;
      memset(&qctl, 0, sizeof(qctl));
      long qlen = 64;
      int rc = tpdequeue("QSPACE", "JOBS", &qctl, &qmsg, &qlen, TPQPEEK);
      if (rc == -1 && tperrno == TPEDIAGNOSTIC &&
          qctl.diagnostic == QMENOMSG) {
        drained = 1;
        break;
      }
      sleep(1);
    }
    printf("S9 queue drained: %s\n", drained ? "yes" : "NO - TIMEOUT");
    tpfree(qmsg);
  }

  tpfree((char *)fb);
  tpfree(reply);
  tpterm();
  return 0;
}
