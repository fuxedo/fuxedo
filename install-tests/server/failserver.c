#include <atmi.h>
#include <userlog.h>

int tpsvrinit(int argc, char **argv) {
  userlog(":TEST: %s called", __func__);
  return -1;
}
void tpsvrdone() { userlog(":TEST: %s called", __func__); }
void FAILSERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
