// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xatmi.h>

#include <cstdarg>
#include <cstdio>

#include <userlog.h>

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
  userlog("%s\n", tplasterr_);
  va_end(ap);
}

void reset_tperrno() {
  tperrno_ = 0;
  tplasterr_[0] = '\0';
}
}  // namespace atmi
}  // namespace fux

int *_tls_tperrno() { return &fux::atmi::tperrno_; }
long *_tls_tpurcode() { return &fux::atmi::tpurcode_; }

static const char *tpstrerror_(int err) {
  switch (err) {
    case TPEABORT:
      return "TPEABORT";
    case TPEBADDESC:
      return "TPEBADDESC";
    case TPEBLOCK:
      return "TPEBLOCK";
    case TPEINVAL:
      return "TPEINVAL";
    case TPELIMIT:
      return "TPELIMIT";
    case TPENOENT:
      return "TPENOENT";
    case TPEOS:
      return "TPEOS";
    case TPEPERM:
      return "TPEPERM";
    case TPEPROTO:
      return "TPEPROTO";
    case TPESVCERR:
      return "TPESVCERR";
    case TPESVCFAIL:
      return "TPESVCFAIL";
    case TPESYSTEM:
      return "TPESYSTEM";
    case TPETIME:
      return "TPETIME";
    case TPETRAN:
      return "TPETRAN";
    case TPGOTSIG:
      return "TPGOTSIG";
    case TPERMERR:
      return "TPERMERR";
    case TPEITYPE:
      return "TPEITYPE";
    case TPEOTYPE:
      return "TPEOTYPE";
    case TPERELEASE:
      return "TPERELEASE";
    case TPEHAZARD:
      return "TPEHAZARD";
    case TPEHEURISTIC:
      return "TPEHEURISTIC";
    case TPEEVENT:
      return "TPEEVENT";
    case TPEDIAGNOSTIC:
      return "TPEDIAGNOSTIC";
    case TPEMIB:
      return "TPEMIB";
    case TPEMATCH:
      return "TPEMATCH";
    default:
      return "";
  }
}
char *tpstrerror(int err) { return const_cast<char *>(tpstrerror_(err)); }
