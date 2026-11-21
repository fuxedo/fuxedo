// Dequeue example: tpdequeue() takes the next available message off
// a /Q queue. run.sh runs ./enqueue first so a message is waiting.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

static int fail(const char *what) {
  fprintf(stderr, "%s failed: %s (%d)\n", what, tpstrerror(tperrno),
          tperrno);
  return 1;
}

int main(void) {
  if (tpinit(NULL) == -1) {
    return fail("tpinit");
  }

  char *msg = tpalloc("STRING", NULL, 64);
  long len = 64;

  // doc:region tpdequeue
  // tpdequeue(qspace, queue, ctl, &data, &len, flags) removes the
  // next visible message and returns it in *data. Messages whose
  // deq_time lies in the future (TPQTIME_* at enqueue time) are
  // skipped, exactly like on the platform. ctl.flags TPQWAIT would
  // wait for a message instead of failing immediately.
  TPQCTL ctl;
  memset(&ctl, 0, sizeof(ctl));
  if (tpdequeue("DOCS", "TASKS", &ctl, &msg, &len, 0) == -1) {
    fprintf(stderr, "tpdequeue failed: %s (%d) diagnostic %ld\n",
            tpstrerror(tperrno), tperrno, ctl.diagnostic);
    return 1;
  }
  // doc:end

  printf("dequeued: %s\n", msg);

  // the queue is empty now: without TPQWAIT the platform fails with
  // TPEDIAGNOSTIC and ctl.diagnostic QMENOMSG
  len = 64;
  int rc = tpdequeue("DOCS", "TASKS", &ctl, &msg, &len, 0);
  if (rc != -1 || tperrno != TPEDIAGNOSTIC || ctl.diagnostic != QMENOMSG) {
    fprintf(stderr, "expected TPEDIAGNOSTIC/QMENOMSG on an empty queue\n");
    return 1;
  }
  printf("empty queue dequeue failed with TPEDIAGNOSTIC/QMENOMSG\n");

  tpfree(msg);
  tpterm();
  return 0;
}
