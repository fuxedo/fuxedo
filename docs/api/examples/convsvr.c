// Conversational server example: one CHAT service, advertised at
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
    if (tprecv(svcinfo->cd, &buf, &len, 0, &revent) == -1) {
      break;  // TPEV_DISCONIMM (tpdiscon) or the peer went away
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
