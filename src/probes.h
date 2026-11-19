#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "config.h"

// Tracing probe points. With sys/sdt.h at build time FUX_PROBE emits a
// SystemTap USDT probe (provider "fuxedo"): one nop in the instruction
// stream until a tracer enables it, zero cost otherwise, no runtime
// dependency. Without the header every probe compiles to nothing.
// Probes are pure observation points and never sit in the data path.
//
// Uprobes work on any build: the ATMI entry points are exported dynamic
// symbols, so bpftrace uprobe:libfuxedo:tpcall keeps working even
// without USDT.

#ifdef HAVE_SYS_SDT_H
#include <sys/sdt.h>
#define FUX_PROBE(name) STAP_PROBE(fuxedo, name)
#define FUX_PROBE1(name, a1) STAP_PROBE1(fuxedo, name, a1)
#define FUX_PROBE2(name, a1, a2) STAP_PROBE2(fuxedo, name, a1, a2)
#define FUX_PROBE3(name, a1, a2, a3) STAP_PROBE3(fuxedo, name, a1, a2, a3)
#else
#define FUX_PROBE(name) \
  do {                  \
  } while (0)
#define FUX_PROBE1(name, a1) \
  do {                       \
  } while (0)
#define FUX_PROBE2(name, a1, a2) \
  do {                           \
  } while (0)
#define FUX_PROBE3(name, a1, a2, a3) \
  do {                               \
  } while (0)
#endif
