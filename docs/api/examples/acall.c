// tpacall()/tpgetrply() example: the two halves of an asynchronous
// call against the TOUPPER service from apisvr.c.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main(int argc, char **argv) {
  const char *input = argc > 1 ? argv[1] : "hello";
  char *req = tpalloc("STRING", NULL, strlen(input) + 1);
  char *rsp = tpalloc("STRING", NULL, strlen(input) + 1);
  long rlen = 0;
  if (req == NULL || rsp == NULL) {
    fprintf(stderr, "tpalloc failed: tperrno=%d\n", tperrno);
    tpfree(req);
    tpfree(rsp);
    return 1;
  }
  strcpy(req, input);

  // doc:region tpacall
  int cd = tpacall("TOUPPER", req, 0, 0);
  if (cd == -1) {
    fprintf(stderr, "tpacall failed: tperrno=%d\n", tperrno);
    tpfree(req);
    tpfree(rsp);
    return 1;
  }
  // doc:end

  // doc:region tpgetrply
  if (tpgetrply(&cd, &rsp, &rlen, 0) == -1) {
    fprintf(stderr, "tpgetrply failed: tperrno=%d\n", tperrno);
    tpfree(req);
    tpfree(rsp);
    return 1;
  }
  // doc:end

  printf("%s -> %s\n", req, rsp);
  tpfree(req);
  tpfree(rsp);
  return 0;
}
