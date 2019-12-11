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
  int ret = tpcall("SERVICE_COMMIT", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(tpurcode == 1);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  memset(rcvbuf, 0, rcvlen);

  ret = tpcall("SERVICE_ABORT", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(tpurcode == 2);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  memset(rcvbuf, 0, rcvlen);

  ret = tpcall("SERVICE_SUSPEND", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(tpurcode == 3);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  ret = tpcall("SERVICE_INPUTS", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(tpurcode == 4);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
