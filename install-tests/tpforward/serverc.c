#include <atmi.h>
#include <userlog.h>

// lives in a group without a transaction manager: calls inside a
// transaction must fail with TPETRAN unless the caller passes TPNOTRAN
void NOTXSSVC(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called lev=%d", __func__, tpgetlev());
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
