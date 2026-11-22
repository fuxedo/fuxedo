#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <userlog.h>

void SERVICE_TPSUCCESS(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}

void SERVICE_TPFAIL(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPFAIL, 2, svcinfo->data, 0, 0);
}

// pins TPSVCINFO.len semantics per buffer type: the logical length
// the caller sent (self-describing buffers report their full length
// when the caller passes 0)
void LENCHECK(TPSVCINFO *svcinfo) {
  char *reply = tpalloc("STRING", NULL, 32);
  sprintf(reply, "%ld", svcinfo->len);
  tpreturn(TPSUCCESS, 0, reply, 0, 0);
}

// a call without any buffer at all: the service sees NULL data with
// len 0 (pinned against Oracle 12.2.2)
void NULLSVC(TPSVCINFO *svcinfo) {
  if (svcinfo->data != NULL || svcinfo->len != 0) {
    userlog(":TEST: FAIL NULLSVC data=%p len=%ld", svcinfo->data,
            svcinfo->len);
    tpreturn(TPFAIL, 0, NULL, 0, 0);
  }
  userlog(":TEST: NULLSVC ok");
  tpreturn(TPSUCCESS, 0, NULL, 0, 0);
}

// always answers with a STRING, whatever the request type was: the
// buffer-switch matrix pins how the reply
// buffer type follows the reply by default and how TPNOCHANGE rejects
// a mismatch
void SWITCHSVC(TPSVCINFO *svcinfo) {
  char *reply = tpalloc("STRING", NULL, 16);
  strcpy(reply, "switched");
  tpreturn(TPSUCCESS, 0, reply, 0, 0);
}
