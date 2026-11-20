// Server lifecycle examples: tpsvrinit/tpsvrdone, the per-dispatch
// thread pair, runtime advertising, and tpreturn. Booted without -s
// advertisements; everything is advertised from tpsvrinit().

#include <stdio.h>
#include <string.h>
#include <atmi.h>
#include <userlog.h>

void GREET(TPSVCINFO *svcinfo) {
  // doc:region tpreturn
  // tpreturn ends the call: TPSUCCESS delivers data as the reply,
  // TPFAIL reports an application failure (tpurcode carries rcode),
  // TPEXIT ends the call and takes the server down
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
  // doc:end
}

// doc:region tpsvrthrinit
int tpsvrthrinit(int argc, char **argv) {
  // called once per dispatch thread, before it takes requests
  return 0;
}
// doc:end

// doc:region tpsvrthrdone
void tpsvrthrdone() {
  // called when a dispatch thread exits
}
// doc:end

// doc:region tpsvrinit
int tpsvrinit(int argc, char **argv) {
  // called once at boot, before the server takes requests: advertise
  // the services this server implements. Functions must be declared at
  // buildserver time; runtime advertising binds names to them
  // doc:region tpadvertise
  if (tpadvertise((char *)"GREETX", GREET) == -1) {
    return -1;
  }
  // doc:end
  // doc:region tpadvertisex
  // tpadvertisex (re)binds a name to a function with extended
  // placement flags (0 = plain); here it takes over the buildserver
  // -s advertisement of GREET
  if (tpadvertisex((char *)"GREET", GREET, 0) == -1) {
    return -1;
  }
  // doc:end
  return 0;
}
// doc:end

// doc:region tpsvrdone
// doc:region tpunadvertise
void tpsvrdone() {
  // called once at orderly shutdown; withdraw the extra advertisement
  tpunadvertise((char *)"GREETX");
  userlog("advsvr done");
}
// doc:end
// doc:end
