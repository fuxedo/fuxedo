// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <stdarg.h>

#include "misc.h"

namespace fux {
bool threaded = false;
}

std::string string_format(const char *fmt, ...) {
  std::string out(128, 0);

  va_list ap;
  va_start(ap, fmt);
  int size = vsnprintf(&out[0], out.size(), fmt, ap);

  if (static_cast<size_t>(size) >= out.size()) {
    out.resize(size + 1);  // Including \0
    va_end(ap);
    va_start(ap, fmt);
    size = vsnprintf(&out[0], out.size(), fmt, ap);
  }
  va_end(ap);
  if (static_cast<size_t>(size) < out.size()) {
    out.resize(size);
  } else if (size == -1) {
    throw new std::runtime_error("Invalid string format");
  }
  return out;
}
