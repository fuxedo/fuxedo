// This file is part of Fuxedo
// Copyright (C) 2018 Aivars Kalvans <aivars.kalvans@gmail.com>
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

#include <tpadm.h>
#include <xatmi.h>

#include "fux.h"
#include "mib.h"

using fux::fml32buf;

static void manage_group(const std::string &operation, fml32buf &in, fml32buf &out) {
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
