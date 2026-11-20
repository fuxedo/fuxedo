// tpcancel() example: abandon an outstanding call; the late reply is
// discarded by the client library when it arrives.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main(int argc, char **argv) {
  const char *input = argc > 1 ? argv[1] : "hello";
  char *req = tpalloc("STRING", NULL, strlen(input) + 1);
  if (req == NULL) {
    fprintf(stderr, "tpalloc failed: tperrno=%d\n", tperrno);
    return 1;
  }
  strcpy(req, input);
  int cd = tpacall("TOUPPER", req, 0, 0);
  if (cd == -1) {
    fprintf(stderr, "tpacall failed: tperrno=%d\n", tperrno);
    tpfree(req);
    return 1;
  }

  // doc:region tpcancel
  if (tpcancel(cd) == -1) {
    fprintf(stderr, "tpcancel failed: tperrno=%d\n", tperrno);
    tpfree(req);
    return 1;
  }
  // doc:end

  printf("cancelled cd=%d\n", cd);
  tpfree(req);
  return 0;
}
