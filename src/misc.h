#pragma once
#include <xatmi.h>

void _tperror(int err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#define TPERROR(err, fmt, args...) \
  _tperror(err, "%s() in %s:%d: " fmt, __func__, __FILE__, __LINE__, ##args)
