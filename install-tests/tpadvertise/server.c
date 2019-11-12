#include <atmi.h>
#include <userlog.h>

void SERVICE(TPSVCINFO *svcinfo) {
  userlog(":TEST: %s called", __func__);
  if (tpunadvertise("SERVICE") == -1) {
    userlog("tpunadvertise failed %s", tpstrerror(tperrno));
    tpreturn(TPFAIL, 1, svcinfo->data, 0, 0);
  }
  if (tpunadvertise("SERVICE") != -1) {
    userlog("tpunadvertise did not fail but should");
    tpreturn(TPFAIL, 1, svcinfo->data, 0, 0);
  }
  tpreturn(TPSUCCESS, 1, svcinfo->data, 0, 0);
}

int tpsvrinit(int argc, char *argv[]) {
  if (tpadvertise("SERVICE", SERVICE) == -1) {
    return -1;
  }
  return 0;
}
