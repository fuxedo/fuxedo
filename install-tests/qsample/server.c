#include <atmi.h>
#include <userlog.h>
#include <stdio.h>
#include <string.h>

// STRING service: receives the forwarded queue message and puts a reply
// on the RPLYQ queue of the same queue space.
void STRING(TPSVCINFO *svcinfo) {
  userlog("QSAMPLE: STRING got %s", (char *)svcinfo->data);

  char *reply = tpalloc("STRING", NULL, 1024);
  snprintf(reply, 1024, "replied:%s", (char *)svcinfo->data);

  TPQCTL ctl = {};
  int rc = tpenqueue("QSPACE", "RPLYQ", &ctl, reply, 0, 0);
  if (rc == -1) {
    userlog("QSAMPLE: reply enqueue failed: %s", tpstrerror(tperrno));
    tpreturn(TPFAIL, 0, svcinfo->data, svcinfo->len, 0);
  }
  strcpy(svcinfo->data, "OK");
  tpreturn(TPSUCCESS, 0, svcinfo->data, 3, 0);
}

int tpsvrinit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  userlog("TEST: tpsvrinit called");
  if (tpadvertise("STRING", STRING) == -1) {
    return -1;
  }
  return 0;
}

void tpsvrdone(void) { userlog("TEST: tpsvrdone called"); }
