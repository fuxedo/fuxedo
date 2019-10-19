#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atmi.h>

int main(int argc, char *argv[]) {
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;
  int ret = tpcall("SERVICE_TPSUCCESS", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);
  assert(tpurcode == 1);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  memset(rcvbuf, 0, rcvlen);

  ret = tpcall("SERVICE_TPFAIL", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPESVCFAIL);
  assert(tpurcode == 2);

  assert(strcmp(rcvbuf, "HELLO") == 0);


  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
