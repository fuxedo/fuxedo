#include <assert.h>
#include <atmi.h>
#include <stddef.h>
#include <stdio.h>
#include <userlog.h>

void SERVICE_COMMIT(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(tpbegin(30, TPNOFLAGS) != -1);
  long olen = 0;
  assert(tpcall("SERVICE1", svcinfo->data, 0, &svcinfo->data, &olen,
                TPNOFLAGS) != -1);
  assert(tpcall("SERVICE2", svcinfo->data, 0, &svcinfo->data, &olen,
                TPNOFLAGS) != -1);
  assert(tpcommit(TPNOFLAGS) != -1);
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}

void SERVICE_ABORT(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(tpbegin(30, TPNOFLAGS) != -1);
  long olen = 0;
  assert(tpcall("SERVICE1", svcinfo->data, 0, &svcinfo->data, &olen,
                TPNOFLAGS) != -1);
  assert(tpcall("SERVICE2", svcinfo->data, 0, &svcinfo->data, &olen,
                TPNOFLAGS) != -1);
  assert(tpabort(TPNOFLAGS) != -1);
  tpreturn(TPSUCCESS, 2, svcinfo->data, 0, 0);
}
