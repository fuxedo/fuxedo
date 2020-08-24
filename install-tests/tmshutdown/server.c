#include <atmi.h>
#include <string.h>
#include <unistd.h>
#include <userlog.h>

static char args[512] = {0};

int tpsvrinit(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    strcat(args, argv[i]);
    strcat(args, " ");
  }
  userlog(":TEST: %s called", __func__);
  return 0;
}
int tpsvrthrinit(int argc, char **argv) {
  userlog(":TEST: %s called", __func__);
  return 0;
}
void tpsvrdone() {
  userlog(":TEST: %s called %s", __func__, args);
  sleep(3);
  userlog(":TEST: %s done %s", __func__, args);
}
void tpsvrthrdone() { userlog(":TEST: %s called", __func__); }
void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
