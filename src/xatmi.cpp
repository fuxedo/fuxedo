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

#include <xatmi.h>

#include <cstdarg>
#include <cstdio>

namespace fux {
namespace atmi {

static thread_local int tperrno_ = 0;
static thread_local long tpurcode_ = 0;

static thread_local char tplasterr_[1024] = {0};

void set_tperrno(int err, const char *fmt, ...) {
  tperrno_ = err;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tplasterr_, sizeof(tplasterr_), fmt, ap);
  va_end(ap);
}

void reset_tperrno() {
  tperrno_ = 0;
  tplasterr_[0] = '\0';
}
}
}

int *_tls_tperrno() { return &fux::atmi::tperrno_; }
long *_tls_tpurcode() { return &fux::atmi::tpurcode_; }

char *tpstrerror(int err) {
  switch (err) {
    case TPEBADDESC:
    case TPEBLOCK:
    case TPEINVAL:
    case TPELIMIT:
    case TPENOENT:
    case TPEOS:
    case TPEPROTO:
    case TPESVCERR:
    case TPESVCFAIL:
    case TPESYSTEM:
    case TPETIME:
    case TPETRAN:
    case TPGOTSIG:
    case TPEITYPE:
    case TPEOTYPE:
    case TPEEVENT:
    case TPEMATCH:
    default:
      return const_cast<char *>("?");
  }
}
