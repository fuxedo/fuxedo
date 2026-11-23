// tpconnect()/tpsend()/tprecv()/tpdiscon() example: a scripted
// conversation with the CHAT service. Connect data ("hello") is the
// first thing the service sees; the client ends with tpdiscon().

#include <stdio.h>
#include <string.h>
#include <atmi.h>

int main() {
  if (tpinit((TPINIT *)NULL) == -1) {
    perror("tpinit");
    return 1;
  }

  char *buf = tpalloc("STRING", NULL, 1024);
  strcpy(buf, "hello");
  long len = 0;
  long revent = 0;

  // doc:region tpconnect
  // a conversation is opened with an explicit direction: TPSENDONLY
  // (caller sends first) or TPRECVONLY (caller waits first); any
  // other flag word, including 0, fails with TPEINVAL
  int cd = tpconnect((char *)"CHAT", buf, 0, TPSENDONLY);
  // doc:end
  if (cd == -1) {
    fprintf(stderr, "tpconnect failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }

  // doc:region tpsend
  // tpsend delivers one message; TPRECVONLY hands send permission to
  // the peer, which learns of it through TPEV_SENDONLY from tprecv
  strcpy(buf, "how are you?");
  if (tpsend(cd, buf, 0, TPRECVONLY, &revent) == -1) {
    fprintf(stderr, "tpsend failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }
  // doc:end

  // doc:region tprecv
  // tprecv waits for one message or event; an event arrives as -1 with
  // tperrno=TPEVENT and revent set (TPEV_SENDONLY: the peer handed send
  // permission over), the data riding with the event is still delivered
  int rc = tprecv(cd, &buf, &len, 0, &revent);
  if (rc == -1 && tperrno != TPEEVENT) {
    fprintf(stderr, "tprecv failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }
  printf("service says: %s (revent=0x%lx)\n", buf, revent);
  // doc:end

  // doc:region tpdiscon
  // tpdiscon tears the conversation down immediately; the service
  // sees TPEV_DISCONIMM from tprecv (a graceful end is an empty
  // message, which maps to the service's tpreturn)
  if (tpdiscon(cd) == -1) {
    fprintf(stderr, "tpdiscon failed: %s (%d)\n", tpstrerror(tperrno),
            tperrno);
    return 1;
  }
  // doc:end

  tpfree(buf);
  tpterm();
  return 0;
}
