#include <assert.h>
#include <atmi.h>
#include <fml32.h>
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
  int ret = tpcall("SERVICE_TPSUCCESS", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "%s\n", tpstrerror(tperrno));
  }
  assert(ret != -1);
  assert(tpurcode == 1);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  memset(rcvbuf, 0, rcvlen);

  ret = tpcall("SERVICE_TPFAIL", sndbuf, 0, &rcvbuf, &rcvlen, 0);
  assert(ret == -1);
  assert(tperrno == TPESVCFAIL);
  assert(tpurcode == 2);

  assert(strcmp(rcvbuf, "HELLO") == 0);

  // TPSVCINFO.len semantics, pinned per type
  char *lenbuf = tpalloc("STRING", NULL, 32);
  long lenlen = 32;
  char *sbuf = tpalloc("STRING", NULL, 16);
  strcpy(sbuf, "HELLO");
  strcpy(sbuf, "HELLO");
  char *cbuf = tpalloc("CARRAY", NULL, 100);
  char *fbuf = tpalloc("FML32", NULL, 2048);
  const struct {
    const char *what;
    char *buf;
    long len;
    const char *want;
  } cases[] = {
      {"str len0", sbuf, 0, "6"},   // len 0: full STRING incl. null
      {"str len5", sbuf, 5, "6"},   // Oracle reports the buffer length
      {"hi len0", sbuf, 0, "3"},
      {"carray100", cbuf, 100, "100"},
      // known difference, recorded in notes/error-code-differences:
      // fuxedo reports the USED length (Fsizeof32), Oracle ships the
      // whole allocation rounded up; assert "positive" cross-platform
      {"fml32 len0", fbuf, 0, "positive"},
  };
  unsigned ci;
  for (ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
    if (ci == 2) strcpy(sbuf, "HI");
    int rc = tpcall("LENCHECK", cases[ci].buf, cases[ci].len, &lenbuf,
                    &lenlen, 0);
    fprintf(stderr, "LENCHECK %s: rc=%d len=%s (want %s)\n", cases[ci].what,
            rc, lenbuf, cases[ci].want);
    // pinned on both platforms: len 0 reports the full buffer length
    // (STRING incl. null); an explicit ilen does NOT shrink the
    // reported length for self-describing types
    assert(rc != -1);
    if (strcmp(cases[ci].want, "positive") == 0) {
      assert(atol(lenbuf) > 0);
    } else {
      assert(strcmp(lenbuf, cases[ci].want) == 0);
    }
  }

  // a call with no buffer at all: the service sees
  // NULL data, len 0, and a NULL reply comes back as success
  rcvbuf = tpalloc("STRING", NULL, 6);
  rcvlen = 6;
  ret = tpcall("NULLSVC", NULL, 0, &rcvbuf, &rcvlen, 0);
  if (ret == -1) {
    fprintf(stderr, "NULLSVC: rc=-1 tperrno=%d\n", tperrno);
  }
  assert(ret != -1);

  // buffer type-switch matrix: by default the reply
  // buffer type follows the reply; TPNOCHANGE rejects a mismatch with
  // TPEOTYPE
  {
    const char *types[] = {"STRING", "CARRAY", "FML32"};
    unsigned ti;
    for (ti = 0; ti < sizeof(types) / sizeof(types[0]); ti++) {
      char *in = tpalloc((char *)types[ti], NULL, 256);
      char *out = tpalloc((char *)types[ti], NULL, 256);
      long ol = 256;
      ret = tpcall("SWITCHSVC", in,
                   strcmp(types[ti], "CARRAY") == 0 ? 256 : 0, &out, &ol, 0);
      if (ret == -1) {
        fprintf(stderr, "SWITCH %s: rc=-1 tperrno=%d\n", types[ti],
                tperrno);
      }
      assert(ret != -1);
      char type[16] = {};
      char sub[16] = {};
      assert(tptypes(out, type, sub) != -1);
      assert(strcmp(type, "STRING") == 0);  // type followed the reply
      tpfree(in);
      tpfree(out);
    }
    // TPNOCHANGE with a non-STRING buffer: TPEOTYPE
    char *in = tpalloc("CARRAY", NULL, 256);
    char *out = tpalloc("CARRAY", NULL, 256);
    long ol = 256;
    ret = tpcall("SWITCHSVC", in, 256, &out, &ol, TPNOCHANGE);
    assert(ret == -1 && tperrno == TPEOTYPE);
    // TPNOCHANGE with the matching type: fine
    out = tpalloc("STRING", NULL, 256);
    ol = 256;
    ret = tpcall("SWITCHSVC", in, 256, &out, &ol, TPNOCHANGE);
    assert(ret != -1);
    tpfree(in);
    tpfree(out);
  }

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
