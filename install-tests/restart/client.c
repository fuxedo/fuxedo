#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;
  // the server may be mid-restart; keep trying like a monitoring caller
  int ret = -1;
  for (int i = 0; i < 100 && ret == -1; i++) {
    ret = tpcall("PING", sndbuf, 0, &rcvbuf, &rcvlen, 0);
    if (ret == -1) {
      usleep(100 * 1000);
    }
  }
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(strcmp(rcvbuf, "PONG") == 0);
  return 0;
}
