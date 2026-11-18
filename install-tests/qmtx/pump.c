// Transactional /Q pump server: moves a message from IN to OUT inside a
// global transaction. Mode comes from the request payload: "commit" or
// "abort" - the client verifies the transactional effect either way.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <atmi.h>
#include <userlog.h>

void PUMP(TPSVCINFO *rqst) {
  char *data = rqst->data;
  TPQCTL ctl = {};
  long len = 64;
  char *msg = tpalloc((char *)"STRING", NULL, 64);

  if (tpbegin(30, TPNOFLAGS) == -1) {
    tpreturn(TPFAIL, 0, rqst->data, 0, 0);
  }

  if (tpdequeue((char *)"QS", (char *)"IN", &ctl, (char **)&msg, &len,
                TPNOFLAGS) == -1) {
    tpabort(TPNOFLAGS);
    strcpy(data, "EMPTY");
    tpreturn(TPSUCCESS, 0, rqst->data, 0, 0);
  }

  TPQCTL enq = {};
  if (tpenqueue((char *)"QS", (char *)"OUT", &enq, msg, len, TPNOFLAGS) ==
      -1) {
    tpabort(TPNOFLAGS);
    strcpy(data, "ENQFAIL");
    tpreturn(TPSUCCESS, 0, rqst->data, 0, 0);
  }

  if (strcmp(data, "commit") == 0) {
    if (tpcommit(TPNOFLAGS) == -1) {
      strcpy(data, "COMMITFAIL");
    } else {
      strcpy(data, "COMMITTED");
    }
  } else {
    if (tpabort(TPNOFLAGS) == -1) {
      strcpy(data, "ABORTFAIL");
    } else {
      strcpy(data, "ABORTED");
    }
  }
  tpreturn(TPSUCCESS, 0, rqst->data, 0, 0);
}
