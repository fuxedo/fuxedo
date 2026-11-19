#include <atmi.h>
#include <fml32.h>
#include <userlog.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_got;
static char g_data[256];
static long g_len;

static void unsol(char *data, long len, long flags) {
  (void)flags;
  g_got++;
  g_len = len;
  snprintf(g_data, sizeof(g_data), "%s", data);
  userlog(":TEST: UNSOL: len=%ld data=%s", len, data);
  // the buffer is owned by the ATMI library and freed after the handler
}

static int selftest() {
  tpsetunsol(unsol);

  long h = tpsubscribe("TEST_EVENT", NULL, NULL, 0);
  if (h < 1000000000L) {
    userlog(":TEST: FAIL subscribe returned %ld tperrno=%d", h, tperrno);
    return 1;
  }

  char *buf = tpalloc("STRING", NULL, 16);
  strcpy(buf, "hello");
  if (tppost("TEST_EVENT", buf, 0, 0) != 0) {
    userlog(":TEST: FAIL tppost tperrno=%d", tperrno);
    return 1;
  }
  int n = tpchkunsol();
  if (n != 1 || g_got != 1 || strcmp(g_data, "hello") != 0) {
    userlog(":TEST: FAIL delivery n=%d got=%d data=%s", n, g_got, g_data);
    return 1;
  }

  if (tpunsubscribe(42, 0) != -1) {
    userlog(":TEST: FAIL bogus unsubscribe succeeded");
    return 1;
  }

  if (tpunsubscribe(h, 0) != 0) {
    userlog(":TEST: FAIL unsubscribe tperrno=%d", tperrno);
    return 1;
  }

  g_got = 0;
  if (tppost("TEST_EVENT", buf, 0, 0) != 0) {
    userlog(":TEST: FAIL tppost after unsub tperrno=%d", tperrno);
    return 1;
  }
  if (tpchkunsol() != 0 || g_got != 0) {
    userlog(":TEST: FAIL delivery after unsubscribe");
    return 1;
  }
  tpfree(buf);

  if (tpsubscribe(NULL, NULL, NULL, 0) != -1) {
    userlog(":TEST: FAIL null subscribe succeeded");
    return 1;
  }

  userlog(":TEST: SELFTEST OK");
  return 0;
}

int main(int argc, char *argv[]) {
  if (tpinit(NULL) == -1) {
    fprintf(stderr, "tpinit failed %d\n", tperrno);
    return 1;
  }
  int rc = selftest();
  tpterm();
  return rc;
}
