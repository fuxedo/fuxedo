#include <atmi.h>
#include <userlog.h>

int tpsvrinit(int argc, char **argv) {
  userlog(":TEST: %s called", __func__);
  return 0;
}
int tpsvrthrinit(int argc, char **argv) {
  userlog(":TEST: %s called", __func__);
  return 0;
}
void tpsvrdone() { userlog(":TEST: %s called", __func__); }
void tpsvrthrdone() { userlog(":TEST: %s called", __func__); }
void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
