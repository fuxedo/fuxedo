#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  if (tpinit((TPINIT *)0) == -1) {
    fprintf(stderr, "tpinit failed: %s\n", tpstrerror(tperrno));
    return 1;
  }

  char *buf = tpalloc("STRING", NULL, 100);
  assert(buf != NULL);
  strcpy(buf, "hello queue");

  // enqueue to the STRING queue, replies land on RPLYQ
  TPQCTL ctl = {};
  ctl.flags = TPQREPLYQ;
  strcpy(ctl.replyqueue, "RPLYQ");
  assert(tpenqueue("QSPACE", "STRING", &ctl, buf, 0, 0) == 0);

  // wait for the forwarded reply
  TPQCTL rctl = {};
  rctl.flags = TPQWAIT;
  long len = 100;
  int rc = -1;
  for (int i = 0; i < 50 && rc == -1; i++) {
    rc = tpdequeue("QSPACE", "RPLYQ", &rctl, (char **)&buf, &len, 0);
  }
  if (rc == -1) {
    fprintf(stderr, "tpdequeue failed: %s diag=%ld\n", tpstrerror(tperrno),
            rctl.diagnostic);
    return 1;
  }
  printf("reply: [%s]\n", buf);
  assert(strncmp(buf, "replied:hello queue", 19) == 0);

  // direct dequeue of an empty queue fails with TPEDIAGNOSTIC and
  // QMENOMSG (pinned against Oracle 12.2.2)
  TPQCTL ectl = {};
  long elen = 100;
  assert(tpdequeue("QSPACE", "RPLYQ", &ectl, (char **)&buf, &elen, 0) == -1);
  assert(tperrno == TPEDIAGNOSTIC);
  assert(ectl.diagnostic == QMENOMSG);

  // an expired message moves to its failure queue (pinned live
  // errorq analog): enqueue with a 1s relative expiry and FAILUREQ,
  // wait for the sweeper, then read it back from ERRQ. fuxedo creates
  // the failure queue on demand (folder model); the pin is the
  // destination, not the creation timing.
  TPQCTL xctl = {};
  xctl.flags = TPQFAILUREQ | TPQEXPTIME_REL;
  xctl.exp_time = 1;
  strcpy(xctl.failurequeue, "ERRQ");
  strcpy(buf, "expire me");
  assert(tpenqueue("QSPACE", "NOFWD", &xctl, buf, 0, 0) == 0);
  // and one without a failure queue simply disappears
  TPQCTL xctl2 = {};
  xctl2.flags = TPQEXPTIME_REL;
  xctl2.exp_time = 1;
  strcpy(buf, "vanish");
  assert(tpenqueue("QSPACE", "NOFWD", &xctl2, buf, 0, 0) == 0);
  sleep(3);
  // the expiry move happens when the home queue is scanned: poke it
  // with a non-blocking dequeue before reading the failure queue
  {
    TPQCTL poke;
    memset(&poke, 0, sizeof(poke));
    long plen = 100;
    int prc = tpdequeue("QSPACE", "NOFWD", &poke, (char **)&buf, &plen, 0);
    fprintf(stderr, "poke NOFWD: rc=%d tperrno=%d diag=%ld\n", prc, tperrno,
            poke.diagnostic);
  }
  long flen = 100;
  int frc = -1;
  int fi;
  for (fi = 0; fi < 20 && frc == -1; fi++) {
    TPQCTL fctl;
    memset(&fctl, 0, sizeof(fctl));
    frc = tpdequeue("QSPACE", "ERRQ", &fctl, (char **)&buf, &flen, 0);
    if (frc == -1) {
      sleep(1);
    }
  }
  if (frc == -1) {
    fprintf(stderr, "ERRQ dequeue: tperrno=%d\n", tperrno);
  }
  assert(frc == 0);
  assert(strcmp(buf, "expire me") == 0);

  tpterm();
  return 0;
}
