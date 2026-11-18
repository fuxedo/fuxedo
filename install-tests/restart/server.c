#include <atmi.h>
#include <userlog.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void PING(TPSVCINFO *svcinfo) {
  strcpy(svcinfo->data, "PONG");
  tpreturn(TPSUCCESS, 0, svcinfo->data, 5, 0);
}

int tpsvrinit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  userlog("TEST: tpsvrinit called");
  if (tpadvertise("PING", PING) == -1) {
    return -1;
  }
  return 0;
}

void tpsvrdone(void) { userlog("TEST: tpsvrdone called"); }
