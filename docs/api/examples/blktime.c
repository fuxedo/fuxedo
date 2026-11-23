// tpsblktime()/tpgblktime() example: set a per-thread blocking
// timeout, read it back, and make a call that is subject to it.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main() {
  if (tpinit((TPINIT *)NULL) == -1) {
    perror("tpinit");
    return 1;
  }

  char *buf = tpalloc("STRING", NULL, 32);
  strcpy(buf, "hello");
  long len = 32;

  // doc:region tpsblktime
  // flags carry the target (TPBLK_ALL persists on the thread, TPBLK_NEXT
  // is consumed by the next blocking call) and the unit (TPBLK_SECOND;
  // 12.x also accepts TPBLK_MILLISECOND, 22.1 rejects it with TPEINVAL).
  // 0 restores waiting forever
  if (tpsblktime(5, TPBLK_ALL | TPBLK_SECOND) == -1) {
    fprintf(stderr, "tpsblktime failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }
  // doc:end

  // doc:region tpgblktime
  // reads the timeout back in the requested unit; flags 0 means the
  // TPBLK_ALL value
  long ms = tpgblktime(TPBLK_MILLISECOND);
  printf("blocking timeout: %ld ms\n", ms);
  // doc:end

  if (tpcall((char *)"TOUPPER", buf, 0, &buf, &len, 0) == -1) {
    fprintf(stderr, "tpcall failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }

  printf("reply: %s\n", buf);
  tpfree(buf);
  tpterm();
  return 0;
}
