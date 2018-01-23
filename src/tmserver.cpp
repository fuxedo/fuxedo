#include "fux.h"

extern "C" void TM(TPSVCINFO *svcinfo) {
  fux::fml32buf buf(svcinfo);

  fux::tpreturn(TPSUCCESS, 0, buf);
}
