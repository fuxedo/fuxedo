#include <atmi.h>
#include <userlog.h>

static int g_prio_calls = 0;
void PRIO(TPSVCINFO *svcinfo) {
  int n = ++g_prio_calls;
  userlog(":TEST: PRIO call=%d prio_in_svc=%d", n, tpgprio());
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}
