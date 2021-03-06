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

#include <gsl/gsl_util>

#include "misc.h"

// FIXME: Linux only
extern const char *__progname;
char *proc_name = const_cast<char *>(__progname);

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
  auto _ = gsl::finally([&] { fclose(fout); });

  char buf[BUFSIZ];
  ssize_t n;
  if (millisec) {
    n = snprintf(buf, sizeof(buf),
                 "%02d%02d%02d.%03d.%s:%s.%d: ", timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec, int(tv.tv_usec / 1000),
                 nodename.c_str(), proc_name, getpid());
  } else {
    n = snprintf(buf, sizeof(buf), "%02d%02d%02d.%s:%s.%d: ", timeinfo.tm_hour,
                 timeinfo.tm_min, timeinfo.tm_sec, nodename.c_str(), proc_name,
                 getpid());
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
