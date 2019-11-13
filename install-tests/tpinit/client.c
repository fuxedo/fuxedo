#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");

  assert(tpinit(NULL) != -1);
  assert(tpinit(NULL) != -1);

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;
  int ret = tpcall("SERVICE", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(tpurcode == 1);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  assert(tpterm() != -1);
  assert(tpterm() != -1);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
