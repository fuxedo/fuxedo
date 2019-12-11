#include <assert.h>
#include <atmi.h>
#include <stddef.h>
#include <userlog.h>

void SERVICE_COMMIT(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(!tpgetlev());
  assert(tpbegin(30, TPNOFLAGS) != -1);
  assert(tpgetlev());
  assert(tpcommit(TPNOFLAGS) != -1);
  assert(!tpgetlev());

  assert(tx_set_transaction_control(TX_CHAINED) == TX_OK);
  assert(tpbegin(30, TPNOFLAGS) != -1);
  assert(tpgetlev());
  assert(tpcommit(TPNOFLAGS) != -1);
  assert(tpgetlev());

  assert(tx_set_transaction_control(TX_UNCHAINED) == TX_OK);
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

  assert(tx_set_transaction_control(TX_CHAINED) == TX_OK);
  assert(tpbegin(30, TPNOFLAGS) != -1);
  assert(tpgetlev());
  assert(tpabort(TPNOFLAGS) != -1);
  assert(tpgetlev());

  assert(tx_set_transaction_control(TX_UNCHAINED) == TX_OK);
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

void SERVICE_TX(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);

  assert(tx_set_transaction_timeout(-1) == TX_EINVAL);
  assert(tx_set_transaction_control(666) == TX_EINVAL);
  assert(tx_set_commit_return(666) == TX_EINVAL);

  assert(tx_set_transaction_control(TX_CHAINED) == TX_OK);
  assert(tx_set_transaction_control(TX_UNCHAINED) == TX_OK);
  assert(tx_set_commit_return(TX_COMMIT_DECISION_LOGGED) == TX_OK);
  assert(tx_set_commit_return(TX_COMMIT_COMPLETED) == TX_OK);

  assert(tpbegin(30, 0) != -1);

  assert(tx_set_transaction_control(TX_CHAINED) == TX_OK);
  assert(tx_set_transaction_control(TX_UNCHAINED) == TX_OK);

  assert(tpcommit(0) != -1);
  tpreturn(TPSUCCESS, 5, svcinfo->data, 0, 0);
}
