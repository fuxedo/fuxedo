// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

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

  const char *ULOGPFX = std::getenv("ULOGPFX");
  if (ULOGPFX == nullptr) {
    ULOGPFX = "ULOG";
  }

  const char *ULOGMILLISEC = std::getenv("ULOGMILLISEC");
  bool millisec = ULOGMILLISEC != nullptr && strcmp(ULOGMILLISEC, "y");

  struct timeval tv;
  struct tm timeinfo;

  gettimeofday(&tv, nullptr);
  localtime_r(&tv.tv_sec, &timeinfo);

  char logfile[FILENAME_MAX + 1];
  snprintf(logfile, sizeof(logfile), "%s.%02d%02d%02d", ULOGPFX,
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
