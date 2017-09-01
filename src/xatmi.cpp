#include <xatmi.h>

#include <cstdarg>
#include <cstdio>

static thread_local char tplasterr_[1024] = {0};
static thread_local int tperrno_ = 0;

int *_tperrno_tls() { return &tperrno_; }

void _tperror(int err, const char *fmt, ...) {
  tperrno_ = err;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tplasterr_, sizeof(tplasterr_), fmt, ap);
  va_end(ap);
}
