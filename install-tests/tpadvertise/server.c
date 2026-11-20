#include <assert.h>
#include <atmi.h>
#include <stddef.h>
#include <userlog.h>

void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  assert(tpunadvertise("SERVICE") != -1);
  assert(tpunadvertise("SERVICE") == -1 && tperrno == TPENOENT);
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}
void SERVICE2(TPSVCINFO *svcinfo) {}

int tpsvrinit(int argc, char *argv[]) {
  assert(tpadvertise(NULL, SERVICE) == -1 && tperrno == TPEINVAL);
  assert(tpadvertise("", SERVICE) == -1 && tperrno == TPEINVAL);
  assert(tpadvertise("SERVICE", NULL) == -1 && tperrno == TPEINVAL);
  assert(tpunadvertise(NULL) == -1 && tperrno == TPEINVAL);
  assert(tpunadvertise("") == -1 && tperrno == TPEINVAL);

  if (tpadvertise("SERVICE", SERVICE) == -1) {
    return -1;
  }
  assert(tpadvertise("SERVICE", SERVICE2) == -1 && tperrno == TPEMATCH);
  // the tpadvertisex placement flags need their prerequisites: a
  // secondary-queue advertisement on a server without SECONDARYRQ=Y
  // fails with TPENOSECONDARYRQ
  assert(tpadvertisex("NOSEC", SERVICE, TPSECONDARYRQ) == -1 &&
         tperrno == TPENOSECONDARYRQ);
  assert(tpadvertisex("SING", SERVICE, TPSINGLETON) != -1);
  assert(tpunadvertise("SING") != -1);
  return 0;
}
