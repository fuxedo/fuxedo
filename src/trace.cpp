// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <userlog.h>

#include <cstdlib>

#include "misc.h"

namespace fux::trace {

static bool env_enabled() {
  const char *tmtrace = std::getenv("TMTRACE");
  return tmtrace != nullptr && tmtrace[0] != '\0';
}

static thread_local bool tls_enabled = env_enabled();

bool enabled() { return tls_enabled; }

void enable(bool on) { tls_enabled = on; }

void event(const char *fmt, ...) {
  if (!tls_enabled) {
    return;
  }
  char body[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  userlog("TRACE:at: %s", body);
}

}  // namespace fux::trace
