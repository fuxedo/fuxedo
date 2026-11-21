// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <stdarg.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "Uunix.h"
#include "misc.h"

namespace fux {
bool threaded = false;
}

static thread_local int uunixerr = 0;

int *Uunixerr_addr(void) { return &uunixerr; }

const char *Uunixmsg[] = {"success", "general failure", nullptr};

void Uunix_err(const char *opname) {
  (void)opname;
  uunixerr = errno;
}

char *Ustrerror(int err) {
  static thread_local char buf[128];
  auto *msg = strerror(err);
  std::strncpy(buf, msg, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}

char *tuxgetenv(const char *name) { return std::getenv(name); }

int tuxputenv(const char *str) {
  auto *eq = std::strchr(str, '=');
  if (eq == nullptr || eq == str) {
    return -1;
  }
  std::string name(str, eq - str);
  return setenv(name.c_str(), eq + 1, 1);
}

int tuxreadenv(const char *file, const char *prefix) {
  std::ifstream in(file);
  if (!in) {
    return -1;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0) {
      continue;
    }
    std::string name = prefix ? std::string(prefix) + line.substr(0, eq)
                              : line.substr(0, eq);
    setenv(name.c_str(), line.substr(eq + 1).c_str(), 1);
  }
  return 0;
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
