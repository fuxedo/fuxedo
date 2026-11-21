#include <atmi.h>
#include <fml32.h>
#include <tpadm.h>
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

  // FML32 filter: only postings matching the boolean expression arrive
  g_got = 0;
  FBFR32 *f = (FBFR32 *)tpalloc("FML32", NULL, 1024);
  long op50 = 50, op200 = 200;
  Fchg32(f, TA_FLAGS, 0, (char *)&op50, 0);
  long hf = tpsubscribe("TEST_EVENT", "TA_FLAGS>100", NULL, 0);
  if (hf < 1000000000L) {
    userlog(":TEST: FAIL filter subscribe %ld tperrno=%d", hf, tperrno);
    return 1;
  }
  if (tppost("TEST_EVENT", (char *)f, 0, 0) != 0) {
    userlog(":TEST: FAIL filter tppost tperrno=%d", tperrno);
    return 1;
  }
  if (tpchkunsol() != 0 || g_got != 0) {
    userlog(":TEST: FAIL non-matching delivery got=%d", g_got);
    return 1;
  }
  Fchg32(f, TA_FLAGS, 0, (char *)&op200, 0);
  if (tppost("TEST_EVENT", (char *)f, 0, 0) != 0) {
    userlog(":TEST: FAIL filter tppost2 tperrno=%d", tperrno);
    return 1;
  }
  if (tpchkunsol() != 1 || g_got != 1) {
    userlog(":TEST: FAIL matching delivery got=%d", g_got);
    return 1;
  }
  if (tpunsubscribe(hf, 0) != 0) {
    userlog(":TEST: FAIL filter unsubscribe tperrno=%d", tperrno);
    return 1;
  }
  tpfree((char *)f);

  userlog(":TEST: SELFTEST OK");
  return 0;
}

// broadcast listener: joins with an optional cltname, waits for one
// unsolicited message, records what arrived in the outfile
static int blisten(const char *cltname, const char *outfile) {
  if (cltname != nullptr && strcmp(cltname, "-") != 0) {
    TPINIT init = {};
    snprintf(init.cltname, sizeof(init.cltname), "%s", cltname);
    if (tpinit(&init) == -1) {
      return 1;
    }
  } else if (tpinit(NULL) == -1) {
    return 1;
  }
  tpsetunsol(unsol);
  FILE *out = fopen(outfile, "w");
  for (int i = 0; i < 150 && g_got == 0; i++) {
    tpchkunsol();
    usleep(100000);
  }
  if (g_got) {
    fprintf(out, "GOT %s\n", g_data);
  } else {
    fprintf(out, "NONE\n");
  }
  fclose(out);
  tpterm();
  return 0;
}

// broadcast trigger: "targeted" filters by cltname, "all" broadcasts
static int btrigger(const char *mode) {
  int rc = 1;
  if (strcmp(mode, "targeted") == 0) {
    TPINIT init = {};
    snprintf(init.cltname, sizeof(init.cltname), "trigger");
    if (tpinit(&init) == -1) {
      return 1;
    }
  } else if (tpinit(NULL) == -1) {
    return 1;
  }
  char *buf = tpalloc("STRING", NULL, 16);

  if (strcmp(mode, "targeted") == 0) {
    strcpy(buf, "targeted");
    rc = tpbroadcast(NULL, NULL, "listenerA", buf, 0, 0);
  } else {
    strcpy(buf, "toall");
    rc = tpbroadcast(NULL, NULL, NULL, buf, 0, 0);
  }
  tpfree(buf);
  tpterm();
  return rc == -1 ? 1 : 0;
}

// holds a subscription for a while: lets the fixture inspect the live
// subscription table through tmadmin printevents
static int esub() {
  long h = tpsubscribe("TEST_EVENT", NULL, NULL, 0);
  if (h < 1000000000L) {
    userlog(":TEST: FAIL esub subscribe %ld tperrno=%d", h, tperrno);
    return 1;
  }
  printf("%ld\n", h);
  fflush(stdout);
  sleep(8);
  if (tpunsubscribe(h, 0) != 0) {
    return 1;
  }
  userlog(":TEST: ESUB DONE");
  return 0;
}

// flag-validation probes against the live event/transaction machinery;
// the Makefile greps the pinned answers (both platforms must agree)
static int flagprobes() {
  char *buf = tpalloc("STRING", NULL, 16);
  strcpy(buf, "hi");
  CLIENTID cid;
  memset(&cid, 0, sizeof(cid));

  tperrno = 0;
  int rc = tppost("FLAGS_EVENT", buf, 0, TPNOBLOCK);
  userlog(":TEST: tppost_flags rc=%d tperrno=%d", rc, tperrno);

  tperrno = 0;
  rc = tpnotify(&cid, buf, 0, TPNOBLOCK);
  userlog(":TEST: tpnotify_flags rc=%d tperrno=%d", rc, tperrno);

  tperrno = 0;
  rc = tpbroadcast(NULL, NULL, NULL, buf, 0, TPNOBLOCK);
  userlog(":TEST: tpbroadcast_flags rc=%d tperrno=%d", rc, tperrno);

  long h;
  tperrno = 0;
  h = tpsubscribe("FLAGS_EVENT", NULL, NULL, TPTRAN);
  userlog(":TEST: subscribe_badbit rc=%ld tperrno=%d", h, tperrno);
  tperrno = 0;
  h = tpsubscribe("FLAGS_EVENT", NULL, NULL, TPEVSERVICE | TPEVQUEUE);
  userlog(":TEST: subscribe_both rc=%ld tperrno=%d", h, tperrno);
  tperrno = 0;
  h = tpsubscribe("FLAGS_EVENT", NULL, NULL, TPEVPERSIST);
  userlog(":TEST: subscribe_persist rc=%ld tperrno=%d", h, tperrno);
  if (h != -1) {
    tpunsubscribe(h, 0);
  }

  tperrno = 0;
  rc = tpbegin(30, 0);
  userlog(":TEST: tpbegin_ok rc=%d tperrno=%d", rc, tperrno);
  if (rc != -1) {
    tpabort(0);
  }
  tperrno = 0;
  rc = tpbegin(30, TPNOBLOCK);
  userlog(":TEST: tpbegin_flags rc=%d tperrno=%d", rc, tperrno);
  tperrno = 0;
  rc = tpcommit(TPNOBLOCK);
  userlog(":TEST: tpcommit_flags rc=%d tperrno=%d", rc, tperrno);
  tperrno = 0;
  rc = tpabort(TPSIGRSTRT);
  userlog(":TEST: tpabort_flags rc=%d tperrno=%d", rc, tperrno);

  tpfree(buf);
  userlog(":TEST: FLAGPROBES DONE");
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc > 3 && strcmp(argv[1], "blisten") == 0) {
    return blisten(argv[2], argv[3]);
  }
  if (argc > 2 && strcmp(argv[1], "btrigger") == 0) {
    return btrigger(argv[2]);
  }
  if (tpinit(NULL) == -1) {
    fprintf(stderr, "tpinit failed %d\n", tperrno);
    return 1;
  }
  int rc = 0;
  if (argc > 1 && strcmp(argv[1], "esub") == 0) {
    rc = esub();
  } else if (argc > 1 && strcmp(argv[1], "flags") == 0) {
    rc = flagprobes();
  } else {
    rc = selftest();
  }
  tpterm();
  return rc;
}
