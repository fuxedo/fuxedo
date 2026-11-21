#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>

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
    rc = tpdequeue("QSPACE", "RPLYQ", &rctl, (char **)&buf, &len, TPQWAIT);
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

  tpterm();
  return 0;
}
