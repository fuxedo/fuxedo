// This file is part of Fuxedo
// Copyright (C) 2018 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <tpadm.h>
#include <xatmi.h>

#include "fux.h"
#include "mib.h"

using fux::fml32buf;

static void manage_group(const std::string &operation, fml32buf &in,
                         fml32buf &out) {
  tuxconfig tuxcfg;
  tuxcfg.size = 0;
  tuxcfg.ipckey = 0;
  tuxcfg.maxservers = 1;
  tuxcfg.maxservices = 1;
  tuxcfg.maxgroups = 1;
  tuxcfg.maxqueues = 1;
  mib m(tuxcfg, fux::mib::in_heap());

  auto groups = m.groups();

  if (operation == "GET") {
  } else if (operation == "SET") {
  }
}

int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags) {
  fml32buf in(&inbuf);
  fml32buf out(outbuf);

  auto klass = in.get<std::string>(TA_CLASS, 0);
  auto operation = in.get<std::string>(TA_OPERATION, 0);
  if (klass == "T_GROUP") {
    manage_group(operation, in, out);
  }
  // TA_OPERATION: GET, GETNEXT, SET
  // TA_CLASS
  // TA_CURSOR
  // TA_OCCURS
  // TA_FLAGS

  return -1;
}
