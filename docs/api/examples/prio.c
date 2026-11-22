// tpsprio()/tpgprio() example: the priority attribute rides with the
// call and is readable inside the service. Values are never
// range-checked and dispatch order is unaffected - both pinned live
// against Oracle 12.2.2 and 22.1.

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main() {
  if (tpinit((TPINIT *)NULL) == -1) {
    perror("tpinit");
    return 1;
  }

  char *buf = tpalloc("STRING", NULL, 32);
  strcpy(buf, "hello");
  long len = 32;

  // doc:region tpsprio
  // sets the priority attribute carried by the next calls; flags are
  // 0 (relative) or TPABSOLUTE, the value itself is never
  // range-checked, and dispatch order is unaffected - the value is
  // an attribute readable in the service, not a scheduling knob
  if (tpsprio(77, TPABSOLUTE) == -1) {
    fprintf(stderr, "tpsprio failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }
  // doc:end

  // doc:region tpgprio
  // client-side: reads back the value set by tpsprio() (fuxedo) - on
  // Oracle Tuxedo it reads -1 regardless. Inside a service it returns
  // the invoking call's priority, 50 when the caller never set one
  printf("current priority: %d\n", tpgprio());
  // doc:end

  if (tpcall((char *)"PRIO", buf, 0, &buf, &len, 0) == -1) {
    fprintf(stderr, "tpcall failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }

  tpfree(buf);
  tpterm();
  return 0;
}
