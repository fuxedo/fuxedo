#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;
  int ret = tpcall("SERVICEA", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);
  assert(strcmp(rcvbuf, "HELLO") == 0);

  ret = tpcall("FAILSERVICE", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPESVCERR);

  ret = tpcall("LOCALSERVICE", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);
  assert(strcmp(rcvbuf, "HELLO") == 0);

  // a server-side transaction aborted by the failed forward: the caller
  // sees TPESVCERR (pinned against Oracle 12.2.2)
  ret = tpcall("TXFWDFAIL", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPESVCERR);

  // TPNOREPLY forward: the target still runs, but the caller is released
  // with an error instead of blocking forever (pinned against Oracle)
  ret = tpcall("FWDNOREPLY", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPESVCERR);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
