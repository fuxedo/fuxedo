#include <atmi.h>
#include <userlog.h>
#include <stddef.h>
#include <assert.h>

void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(tpunadvertise("SERVICE") != -1);
  assert(tpunadvertise("SERVICE") == -1 && tperrno == TPENOENT);
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}

int tpsvrinit(int argc, char *argv[]) {
  assert(tpadvertise(NULL, SERVICE) == -1 && tperrno == TPEINVAL);
  assert(tpadvertise("", SERVICE) == -1 && tperrno == TPEINVAL);
  assert(tpadvertise("SERVICE", NULL) == -1 && tperrno == TPEINVAL);

  if (tpadvertise("SERVICE", SERVICE) == -1) {
    return -1;
  }
  return 0;
}
