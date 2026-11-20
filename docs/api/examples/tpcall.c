// tpcall() example: a synchronous request/response call. With one
// argument it calls TOUPPER; with two, the first is the service name
// (used to exercise the FORWARD service from apisvr.c).

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main(int argc, char **argv) {
  const char *svc = argc > 2 ? argv[1] : "TOUPPER";
  const char *input = argc > 2 ? argv[2] : (argc > 1 ? argv[1] : "hello");
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
  if (tpcall((char *)svc, req, 0, &rsp, &rlen, 0) == -1) {
    fprintf(stderr, "tpcall failed: tperrno=%d\n", tperrno);
    tpfree(req);
    tpfree(rsp);
    return 1;
  }
  printf("%s -> %s\n", req, rsp);
  tpfree(req);
  tpfree(rsp);
  return 0;
}
