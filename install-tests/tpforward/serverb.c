#include <atmi.h>
#include <userlog.h>

void SERVICEB(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
