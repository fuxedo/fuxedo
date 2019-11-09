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
  int ret = tpcall("SERVICE", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  memset(rcvbuf, 0, rcvlen);

  ret = tpcall("MTSERVICE", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  memset(rcvbuf, 0, rcvlen);

  ret = tpcall("FAILSERVICE", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPENOENT);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
