#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);
  strcpy(sndbuf, "HELLO");

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;
  int cd = tpacall("SLEEP", sndbuf, 0, 0);
  assert(cd > 0);

  time_t start;

  start = time(NULL);

  assert(tpsblktime(3, TPBLK_ALL) != -1);
  assert(tpsblktime(1, TPBLK_NEXT) != -1);
  int ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETIME);
  assert(time(NULL) - start >= 1);
  assert(time(NULL) - start <  3);

  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPETIME);
  assert(time(NULL) - start >= 3);

  sleep(6);

  ret = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(ret != -1);

  cd = tpacall("SLEEP", sndbuf, 0, 0);
  assert(cd > 0);

  for (;;) {
    start = time(NULL);
    ret = tpacall("SERVICE", sndbuf, 0, 0);
    if (ret == -1) {
      assert(tperrno == TPETIME);
      assert(time(NULL) - start >= 3);

      ret = tpacall("SERVICE", sndbuf, 0, TPNOBLOCK);
      assert(ret == -1);
      assert(tperrno == TPEBLOCK);

      start = time(NULL);
      assert(tpsblktime(1, TPBLK_NEXT) != -1);
      ret = tpacall("SERVICE", sndbuf, 0, 0);
      assert(ret == -1);
      assert(tperrno == TPETIME);
      assert(time(NULL) - start >= 1);
      assert(time(NULL) - start < 3);

      sleep(6);

      ret = tpacall("SERVICE", sndbuf, 0, 0);
      assert(ret != -1);
      break;
    }
  }

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
