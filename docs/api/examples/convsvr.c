// Conversational server example: one CHAT service, advertised at
#include <stdio.h>
// buildserver time. The client connected with TPSENDONLY, so the
// service must tprecv() before it may tpsend(). An empty message is
// the polite end; tpdiscon shows up as TPEV_DISCONIMM.

#include <string.h>
#include <atmi.h>
#include <userlog.h>

void CHAT(TPSVCINFO *svcinfo) {
  // connect data arrives as svcinfo->data, like a request buffer
  userlog("CHAT: connect data: %s", svcinfo->data);
  char *buf = tpalloc("STRING", NULL, 1024);
  long len = 0;
  long revent = 0;

  while (1) {
    int rc = tprecv(svcinfo->cd, &buf, &len, 0, &revent);
    // events answer -1 with tperrno=TPEEVENT and the data delivered;
    // only a real error or the disconnect event ends the loop
    if (rc == -1 && tperrno != TPEEVENT) {
      break;  // the peer went away
    }
    if (revent & TPEV_DISCONIMM) {
      break;
    }
    if (len <= 1) {
      // an empty message ends the conversation politely
      tpreturn(TPSUCCESS, 0, (char *)"done", 0, 0);
    }
    if (tpsend(svcinfo->cd, buf, 0, TPRECVONLY, &revent) == -1) {
      break;
    }
  }
  tpreturn(TPSUCCESS, 0, NULL, 0, 0);
}

// PRIO: answers with the priority attribute of the invoking call,
// surfaced through tpgprio() (the docs example for tpsprio/tpgprio)
void PRIO(TPSVCINFO *svcinfo) {
  // tpreturn data must be a tpalloc()'d buffer (or svcinfo->data), the
  // library reads the buffer header at return time
  char *reply = tpalloc("STRING", NULL, 32);
  snprintf(reply, 32, "prio=%d", tpgprio());
  tpreturn(TPSUCCESS, 0, reply, 0, 0);
}
