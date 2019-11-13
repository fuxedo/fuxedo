#include <assert.h>
#include <atmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  char *sndbuf = tpalloc("STRING", NULL, 6);
  assert(sndbuf != NULL);

  char *rcvbuf = tpalloc("STRING", NULL, 6);
  assert(rcvbuf != NULL);

  long rcvlen = 6;

  strcpy(sndbuf, "00001");
  int cd1 = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd1 != -1);

  strcpy(sndbuf, "00002");
  int cd2 = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd2 != -1);

  strcpy(sndbuf, "00003");
  int cd3 = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd3 != -1);

  int rc;

  rc = tpgetrply(&cd2, &rcvbuf, &rcvlen, 0);
  assert(rc != -1);
  assert(strcmp(rcvbuf, "00002") == 0);

  rc = tpgetrply(&cd3, &rcvbuf, &rcvlen, 0);
  assert(rc != -1);
  assert(strcmp(rcvbuf, "00003") == 0);

  rc = tpgetrply(&cd1, &rcvbuf, &rcvlen, 0);
  assert(rc != -1);
  assert(strcmp(rcvbuf, "00001") == 0);

  for (int i = 0; i < 5; i++) {
    strcpy(sndbuf, "0000X");
    int cd = tpacall("SERVICE", sndbuf, 0, 0);
    assert(cd != -1);
  }

  for (int i = 0; i < 5; i++) {
    int cd = -1;
    rc = tpgetrply(&cd, &rcvbuf, &rcvlen, TPGETANY);
    assert(rc != -1);
    assert(cd > 0);
    assert(strcmp(rcvbuf, "0000X") == 0);
  }

  assert(tpcancel(666) == -1);
  assert(tperrno == TPEBADDESC);

  int cd = tpacall("SERVICE", sndbuf, 0, 0);
  assert(cd != -1);
  assert(tpcancel(cd) != -1);
  rc = tpgetrply(&cd, &rcvbuf, &rcvlen, 0);
  assert(rc == -1);
  assert(tperrno == TPEBADDESC);

  tpfree(sndbuf);
  tpfree(rcvbuf);
  return 0;
}
