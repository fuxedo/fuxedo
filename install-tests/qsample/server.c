#include <atmi.h>
#include <userlog.h>
#include <stdio.h>
#include <string.h>

// STRING service: receives the forwarded queue message and returns the
// reply; TMQFORWARD enqueues it on the reply queue recorded with the
// message (the platform model).
void STRING(TPSVCINFO *svcinfo) {
  userlog("QSAMPLE: STRING got %s", (char *)svcinfo->data);

  char *reply = tpalloc("STRING", NULL, 1024);
  snprintf(reply, 1024, "replied:%s", (char *)svcinfo->data);
  tpreturn(TPSUCCESS, 0, reply, strlen(reply) + 1, 0);
  tpfree(reply);
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
