// tpinit()/tpterm() example: explicit session control. A regular
// client can skip both; the first ATMI call joins the application with
// defaults and the detach happens at process exit.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main(int argc, char **argv) {
  const char *input = argc > 1 ? argv[1] : "hello";

  // doc:region tpinit
  // NULL joins with defaults; a TPINIT carries usrname, cltname,
  // passwd, grpname, and identification data for apps that require it
  if (tpinit((TPINIT *)NULL) == -1) {
    // doc:region tperrno
    // tperrno holds the code of the last failed ATMI call; a
    // successful call does not reset it, so read it right after the
    // failure
    int code = tperrno;
    // doc:end
    // doc:region tpstrerror
    // tpstrerror renders a tperrno code as text
    fprintf(stderr, "tpinit failed: %s (%d)\n", tpstrerror(code), code);
    // doc:end
    return 1;
  }
  // doc:end

  char *req = tpalloc("STRING", NULL, strlen(input) + 1);
  char *rsp = tpalloc("STRING", NULL, strlen(input) + 1);
  long rlen = 0;
  int rc = 0;
  if (req == NULL || rsp == NULL) {
    fprintf(stderr, "tpalloc failed: tperrno=%d\n", tperrno);
    rc = 1;
  } else {
    strcpy(req, input);
    if (tpcall("TOUPPER", req, 0, &rsp, &rlen, 0) == -1) {
      fprintf(stderr, "tpcall failed: tperrno=%d\n", tperrno);
      rc = 1;
    } else {
      printf("%s -> %s\n", req, rsp);
    }
  }
  tpfree(req);
  tpfree(rsp);

  // doc:region tpterm
  tpterm();
  // doc:end
  return rc;
}
