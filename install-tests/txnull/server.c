#include <atmi.h>
#include <userlog.h>
#include <assert.h>

void SERVICE_COMMIT(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(!tpgetlev());
  assert(tpbegin(30, TPNOFLAGS) != -1);
  assert(tpgetlev());
  assert(tpcommit(TPNOFLAGS) != -1);
  assert(!tpgetlev());
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}

void SERVICE_ABORT(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(!tpgetlev());
  assert(tpbegin(30, TPNOFLAGS) != -1);
  assert(tpgetlev());
  assert(tpabort(TPNOFLAGS) != -1);
  assert(!tpgetlev());
  tpreturn(TPSUCCESS, 2, svcinfo->data, 0, 0);
}

void SERVICE_SUSPEND(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(!tpgetlev());
  assert(tpbegin(30, TPNOFLAGS) != -1);
  assert(tpgetlev());
  TPTRANID t;
  assert(tpsuspend(&t, 0) != -1);
  assert(!tpgetlev());
  assert(tpresume(&t, 0) != -1);
  assert(tpgetlev());
  assert(tpcommit(TPNOFLAGS) != -1);
  assert(!tpgetlev());
  tpreturn(TPSUCCESS, 3, svcinfo->data, 0, 0);
}
