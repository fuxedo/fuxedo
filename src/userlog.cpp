// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <userlog.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>

#include "misc.h"

// Resolved from /proc/self/comm at first use; an application may still
// assign proc_name to override it (public contract from userlog.h).
static char proc_name_buf[64] = {};
char *proc_name = proc_name_buf;

static const char *proc_name_get() {
  if (proc_name != proc_name_buf) {
    return proc_name;
  }
  FILE *f = fopen("/proc/self/comm", "r");
  if (f != nullptr) {
    if (fgets(proc_name_buf, sizeof(proc_name_buf), f) != nullptr) {
      size_t n = strlen(proc_name_buf);
      if (n > 0 && proc_name_buf[n - 1] == '\n') {
        proc_name_buf[n - 1] = 0;
      }
    }
    fclose(f);
  }
  if (proc_name_buf[0] == 0) {
    strcpy(proc_name_buf, "unknown");
  }
  return proc_name_buf;
}

static std::string getname() {
  struct utsname buf;
  if (uname(&buf) == -1) {
    return "<unknown>";
  }
  return buf.nodename;
}

int userlog(const char *fmt, ...) {
  static std::string nodename = getname();

  auto ULOGPFX = fux::util::getenv("ULOGPFX", "ULOG");
  bool millisec = fux::util::getenv("ULOGMILLISEC", "n") == "y";

  struct timeval tv;
  struct tm timeinfo;

  gettimeofday(&tv, nullptr);
  localtime_r(&tv.tv_sec, &timeinfo);

  char logfile[FILENAME_MAX + 1];
  snprintf(logfile, sizeof(logfile), "%s.%02d%02d%02d", ULOGPFX.c_str(),
           timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year % 100);

  FILE *fout = fopen(logfile, "a");
  if (fout == nullptr) {
    return -1;
  }
  struct scope_exit {
    FILE *f;
    ~scope_exit() { fclose(f); }
  } close_fout{fout};

  char buf[BUFSIZ];
  ssize_t n;
  if (millisec) {
    n = snprintf(buf, sizeof(buf),
                 "%02d%02d%02d.%03d.%s!%s.%d: ", timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec, int(tv.tv_usec / 1000),
                 nodename.c_str(), proc_name_get(), getpid());
  } else {
    n = snprintf(buf, sizeof(buf), "%02d%02d%02d.%s!%s.%d: ", timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec, nodename.c_str(),
                 proc_name_get(), getpid());
  }

  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
  va_end(ap);
  buf[n++] = '\n';
  buf[n] = 0;
  fwrite(buf, n, 1, fout);

  return 0;
}
