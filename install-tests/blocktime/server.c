#include <atmi.h>
#include <unistd.h>
#include <userlog.h>

void SLEEP(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  sleep(10);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
