// tpalloc()/tptypes()/tpfree() example: typed buffer lifecycle.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main(void) {
  // doc:region tpalloc
  char *buf = tpalloc("STRING", NULL, 64);
  if (buf == NULL) {
    fprintf(stderr, "tpalloc failed: tperrno=%d\n", tperrno);
    return 1;
  }
  // doc:end

  // doc:region tptypes
  char type[8] = {0};
  char subtype[16] = {0};
  long len = tptypes(buf, type, subtype);
  if (len == -1) {
    fprintf(stderr, "tptypes failed: tperrno=%d\n", tperrno);
    tpfree(buf);
    return 1;
  }
  // doc:end
  printf("type=%s subtype=%s len=%ld\n", type, subtype, len);

  strcpy(buf, "hello");

  // doc:region tpfree
  tpfree(buf);
  // doc:end
  return 0;
}
