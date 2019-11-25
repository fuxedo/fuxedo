#include <assert.h>
#include <atmi.h>
#include <stddef.h>
#include <userlog.h>

int tpsvrinit(int argc, char *argv[]) {
  assert(tpinit(NULL) == -1);
  assert(tperrno == TPEPROTO);
  return 0;
}

void tpsvrdone() {
  assert(tpterm() == -1);
  assert(tperrno == TPEPROTO);
}

void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}
