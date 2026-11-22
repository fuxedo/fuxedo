#include <atmi.h>
#include <userlog.h>

void WORK(TPSVCINFO *svcinfo) {
  userlog(":TEST: WORK called");
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
