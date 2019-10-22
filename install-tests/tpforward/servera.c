#include <atmi.h>
#include <userlog.h>

void SERVICEA(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpforward("SERVICEB", svcinfo->data, 0, 0);
}

void FAILSERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpforward("NOSUCHSERVICE", svcinfo->data, 0, 0);
}

void LOCALSERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpforward("SERVICEA", svcinfo->data, 0, 0);
}
