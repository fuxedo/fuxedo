// transactional propagation through tpforward - fuxedo-only asserts
// (Oracle would need a real resource manager for the group TMS; the
// no-TMS semantics are covered by client.c on both platforms)
#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>

int main() {
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");
  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);
  long rcvlen = 6;

  // a forward joins the caller's transaction: the commit succeeds and
  // TXSVC logs lev=1
  int rc = tpbegin(30, 0);
  assert(rc == 1);
  assert(tpgetlev() == 1);
  int ret = tpcall("FWDTX", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);
  assert(tpgetlev() == 1);
  rc = tpcommit(0);
  assert(rc == 1);
  assert(tpgetlev() == 0);

  // TPNOTRAN: the target runs outside the caller's transaction, the
  // caller's transaction still commits
  rc = tpbegin(30, 0);
  assert(rc == 1);
  ret = tpcall("FWDNOTRAN", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);
  assert(tpgetlev() == 1);
  rc = tpcommit(0);
  assert(rc == 1);
  assert(tpgetlev() == 0);

  // a service in a group without a TMS cannot join a transaction
  rc = tpbegin(30, 0);
  assert(rc == 1);
  ret = tpcall("NOTXSSVC", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETRAN);
  ret = tpcall("NOTXSSVC", sndbuf, 0, &rcvbuf, &rcvlen, TPNOTRAN);
  assert(ret != -1);
  rc = tpcommit(0);
  assert(rc == 1);

  // no-reply calls are forbidden inside a transaction
  rc = tpbegin(30, 0);
  assert(rc == 1);
  ret = tpacall("FWDNOTRAN", sndbuf, 0, TPNOREPLY);
  assert(ret == -1);
  assert(tperrno == TPEPROTO);
  rc = tpabort(0);
  assert(rc == 1);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
