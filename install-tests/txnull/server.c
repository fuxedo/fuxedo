#include <atmi.h>
#include <userlog.h>
#include <assert.h>
#include <stddef.h>

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

void SERVICE_INPUTS(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);

  assert(tpbegin(30, 666) == -1);
  assert(tperrno == TPEINVAL);

  TPTRANID t;
  assert(tpsuspend(NULL, 0) == -1);
  assert(tperrno == TPEINVAL);

  assert(tpsuspend(&t, 666) == -1);
  assert(tperrno == TPEINVAL);

  assert(tpresume(NULL, 0) == -1);
  assert(tperrno == TPEINVAL);

  assert(tpresume(&t, 666) == -1);
  assert(tperrno == TPEINVAL);

  assert(tpcommit(666) == -1);
  assert(tperrno == TPEINVAL);

  assert(tpabort(666) == -1);
  assert(tperrno == TPEINVAL);

  tpreturn(TPSUCCESS, 4, svcinfo->data, 0, 0);
}
