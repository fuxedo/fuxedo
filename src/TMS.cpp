#include <atmi.h>
#include <stdio.h>
#include <xa.h>
#include "servermain.h"

#if defined(__cplusplus)
extern "C" {
#endif
void TM(TPSVCINFO *);
int _tmrunserver(int);
extern struct xa_switch_t tmnull_switch;
extern int _tmbuilt_with_thread_option;
#if defined(__cplusplus)
}
#endif

static struct tmdsptchtbl_t _tmdsptchtbl[] = {{".TM", "TM", TM, 0, 0, NULL},
                                              {NULL, NULL, NULL, 0, 0, NULL}};
static struct tmsvrargs_t tmsvrargs =
    fuxedo_server_args(&tmnull_switch, _tmdsptchtbl, true);
struct tmsvrargs_t *_tmgetsvrargs() { return &tmsvrargs; }

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
