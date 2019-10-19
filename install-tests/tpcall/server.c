#include <userlog.h>
#include <atmi.h>

void SERVICE_TPSUCCESS(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}

void SERVICE_TPFAIL(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPFAIL, 2, svcinfo->data, 0, 0);
}
