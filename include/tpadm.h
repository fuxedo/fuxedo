#pragma once

#include "fml32.h"

#ifdef __cplusplus
extern "C" {
#endif
int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags);
#ifdef __cplusplus
}
#endif
