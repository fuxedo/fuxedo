// Enqueue example: tpenqueue() puts a message on a /Q queue.
// Queue files live in the device named by the QMCONFIG environment
// variable; the sample domain does not need a TMQUEUE server for
// this. run.sh creates the DOCS queue space and the TASKS queue
// before invoking this program.

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
  strcpy(msg, "urgent: ping");

  // doc:region tpenqueue
  // TPQCTL describes optional delivery properties; zero means plain
  // delivery. TPQREPLYQ records a queue name with the message so a
  // reply can be routed there by whoever processes it.
  TPQCTL ctl;
  memset(&ctl, 0, sizeof(ctl));
  ctl.flags = TPQREPLYQ;
  strcpy(ctl.replyqueue, "PINGRPLY");

  // tpenqueue(qspace, queue, ctl, data, len, flags) appends the
  // buffer to the queue. Like tpcall(), STRING buffers pass len 0.
  // Inside a transaction the enqueue is staged with it and undone on
  // abort. On TPEDIAGNOSTIC, ctl.diagnostic carries a QME_* code.
  if (tpenqueue("DOCS", "TASKS", &ctl, msg, 0, 0) == -1) {
    fprintf(stderr, "tpenqueue failed: %s (%d) diagnostic %ld\n",
            tpstrerror(tperrno), tperrno, ctl.diagnostic);
    return 1;
  }
  // doc:end

  printf("enqueued: %s\n", msg);
  tpfree(msg);
  tpterm();
  return 0;
}
