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

// begins a transaction and forwards to a missing service: what does
// the caller see and what happens to the transaction?
void TXFWDFAIL(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called lev=%d", __func__, tpgetlev());
  if (tpbegin(30, 0) == -1) {
    userlog(":TEST: %s tpbegin failed %d", __func__, tperrno);
  }
  userlog(":TEST: %s lev after begin=%d", __func__, tpgetlev());
  tpforward("NOSUCHSERVICE", svcinfo->data, 0, 0);
  userlog(":TEST: %s returned from tpforward?!", __func__);
}

// forwards to TXSVC carrying the caller's transaction (when any)
void FWDTX(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called lev=%d", __func__, tpgetlev());
  tpforward("TXSVC", svcinfo->data, 0, 0);
}

// forwards to TXSVC with TPNOTRAN: the target must run outside the
// caller's transaction
void FWDNOTRAN(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called lev=%d", __func__, tpgetlev());
  tpforward("TXSVC", svcinfo->data, 0, TPNOTRAN);
}

// forwards with TPNOREPLY: the caller must not get a service reply
void FWDNOREPLY(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called lev=%d", __func__, tpgetlev());
  tpforward("SERVICEB", svcinfo->data, 0, TPNOREPLY);
  userlog(":TEST: %s returned from tpforward?!", __func__);
}

// TPNOREPLY to a slow target: when is the caller released?
void FWDNOSLOW(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called lev=%d", __func__, tpgetlev());
  tpforward("SLOWSVC", svcinfo->data, 0, TPNOREPLY);
  userlog(":TEST: %s returned from tpforward?!", __func__);
}
