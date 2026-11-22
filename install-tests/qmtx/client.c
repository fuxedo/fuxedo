// Drives the pump: seed IN, request "commit", verify OUT; seed again,
// request "abort", verify the message stays in IN and OUT stays empty.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atmi.h>

static int count(const char *qname) {
  TPQCTL ctl = {};
  char *msg = tpalloc((char *)"STRING", NULL, 64);
  long len = 64;
  int found = tpdequeue((char *)"QS", (char *)qname, &ctl, (char **)&msg, &len,
                        TPQPEEK) != -1;
  tpfree(msg);
  return found;
}

static void seed(const char *text) {
  TPQCTL ctl = {};
  char *buf = tpalloc((char *)"STRING", NULL, strlen(text) + 1);
  strcpy(buf, text);
  if (tpenqueue((char *)"QS", (char *)"IN", &ctl, buf, 0, TPNOFLAGS) == -1) {
    fprintf(stderr, "seed failed tperrno=%d\n", tperrno);
    exit(1);
  }
  tpfree(buf);
}

static const char *pump(const char *mode) {
  char *rcvbuf = tpalloc((char *)"STRING", NULL, 64);
  long rcvlen = 64;
  char *snd = tpalloc((char *)"STRING", NULL, strlen(mode) + 1);
  strcpy(snd, mode);
  if (tpcall((char *)"PUMP", snd, 0, &rcvbuf, &rcvlen, 0) == -1) {
    fprintf(stderr, "pump call failed tperrno=%d\n", tperrno);
    exit(1);
  }
  const char *r = strdup(rcvbuf);
  tpfree(snd);
  tpfree(rcvbuf);
  return r;
}

static void drain(const char *qname) {
  for (;;) {
    TPQCTL ctl = {};
    char *msg = tpalloc((char *)"STRING", NULL, 64);
    long len = 64;
    int rc = tpdequeue((char *)"QS", (char *)qname, &ctl, (char **)&msg, &len,
                       TPNOFLAGS);
    tpfree(msg);
    if (rc == -1) {
      break;
    }
  }
}

// two overlapping transactions through the multithreaded pump (their
// each side seeds, calls PUMP in its own transaction,
// and commits; the concurrent dispatches on different threads must
// keep the resource-manager branches apart
static int concurrent_side() {
  if (tpinit((TPINIT *)NULL) == -1) {
    return 10;
  }
  // each side's pump call runs its own transaction on the
  // multithreaded server; the two dispatches overlap
  const char *r = pump("commit");
  int ok = r != NULL && strcmp(r, "COMMITTED") == 0;
  tpterm();
  return ok ? 0 : 11;
}

static int concurrent_mode() {
  drain("IN");
  drain("OUT");
  seed("work-a");
  seed("work-b");
  pid_t pid = fork();
  if (pid == 0) {
    _exit(concurrent_side());
  }
  int a = concurrent_side();
  int st = 0;
  waitpid(pid, &st, 0);
  int b = WIFEXITED(st) ? WEXITSTATUS(st) : 20;
  if (a != 0 || b != 0) {
    fprintf(stderr, "concurrent sides failed A=%d B=%d\n", a, b);
    return 4;
  }
  // exact accounting: drain OUT and IN, counting every message
  int out = 0;
  for (;;) {
    TPQCTL ctl = {};
    char *msg = tpalloc((char *)"STRING", NULL, 64);
    long len = 64;
    int rc = tpdequeue((char *)"QS", (char *)"OUT", &ctl, (char **)&msg, &len,
                       TPNOFLAGS);
    tpfree(msg);
    if (rc == -1) {
      break;
    }
    out++;
  }
  if (out != 2 || count("IN") != 0) {
    fprintf(stderr, "concurrency state OUT=%d IN=%d\n", out, count("IN"));
    return 5;
  }
  printf("dispatch-thread concurrency OK\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "concurrent") == 0) {
    return concurrent_mode();
  }
  if (tpinit((TPINIT *)NULL) == -1) {
    fprintf(stderr, "tpinit failed\n");
    return 1;
  }
  drain("IN");
  drain("OUT");
  seed("work-1");
  const char *r1 = pump("commit");
  printf("commit -> %s, OUT=%d IN=%d\n", r1, count("OUT"), count("IN"));
  if (strcmp(r1, "COMMITTED") != 0 || count("OUT") != 1 || count("IN") != 0) {
    fprintf(stderr, "COMMIT CASE FAILED\n");
    return 2;
  }
  TPQCTL drain = {};
  char *gone = tpalloc((char *)"STRING", NULL, 64);
  long glen = 64;
  tpdequeue((char *)"QS", (char *)"OUT", &drain, (char **)&gone, &glen,
            TPNOFLAGS);
  tpfree(gone);

  seed("work-2");
  const char *r2 = pump("abort");
  printf("abort  -> %s, OUT=%d IN=%d\n", r2, count("OUT"), count("IN"));
  if (strcmp(r2, "ABORTED") != 0 || count("OUT") != 0 || count("IN") != 1) {
    fprintf(stderr, "ABORT CASE FAILED\n");
    return 3;
  }
  tpterm();
  printf("qmtx pump OK\n");
  return 0;
}
