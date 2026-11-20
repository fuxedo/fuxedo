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

  // doc:region tpexport
  // tpexport renders a typed buffer into a portable form that
  // tpimport rebuilds
  char exported[256];
  long elen = sizeof(exported);
  if (tpexport(buf, 0, exported, &elen, 0) == -1) {
    fprintf(stderr, "tpexport failed: tperrno=%d\n", tperrno);
    tpfree(buf);
    return 1;
  }
  // doc:end

  // doc:region tpimport
  // tpimport rebuilds the buffer; *obuf points at the target buffer
  // and *olen carries its size (22.x grows the buffer when the data
  // does not fit)
  char *back = tpalloc("STRING", NULL, 128);
  long blen = 128;
  if (back == NULL || tpimport(exported, elen, &back, &blen, 0) == -1) {
    fprintf(stderr, "tpimport failed: tperrno=%d\n", tperrno);
    tpfree(buf);
    tpfree(back);
    return 1;
  }
  printf("roundtrip: %s\n", back);
  tpfree(back);
  // doc:end

  // doc:region tpfree
  tpfree(buf);
  // doc:end
  return 0;
}
