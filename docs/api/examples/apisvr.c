// Sample server for the API reference examples. TOUPPER does the work;
// FORWARD exists to demonstrate tpforward().

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <atmi.h>

void TOUPPER(TPSVCINFO *svcinfo) {
  for (char *p = svcinfo->data; *p != '\0'; p++) {
    *p = (char)toupper((unsigned char)*p);
  }
  tpreturn(TPSUCCESS, 0, svcinfo->data, 0, 0);
}

// doc:region tpforward
void FORWARD(TPSVCINFO *svcinfo) {
  // hand the request to TOUPPER; tpforward does not return
  tpforward((char *)"TOUPPER", svcinfo->data, svcinfo->len, 0);
}
// doc:end
