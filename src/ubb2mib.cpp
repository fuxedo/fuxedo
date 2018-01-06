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

#include <iostream>

#include "mib.h"
#include "misc.h"
#include "ubbreader.h"

template <typename T>
static long checked_get(T &dict, const std::string &key, long min, long max) {
  if (dict.find(key) == dict.end()) {
    throw std::out_of_range(key + " required");
  }
  auto value = std::stol(dict[key]);
  if (value < min || value > max) {
    throw std::out_of_range(key + " out of range");
  }
  return value;
}

template <typename T>
static long checked_get(T &dict, const std::string &key, long min, long max,
                        long defvalue) {
  if (dict.find(key) == dict.end()) {
    return defvalue;
  }
  auto value = std::stol(dict[key]);
  if (value < min || value > max) {
    throw std::out_of_range(key + " out of range");
  }
  return value;
}

void ubb2mib(ubbconfig &u, mib &m) {
  if (u.machines.size() != 1) {
    throw std::logic_error("Excatly one MACHINE entry supported");
  }
  auto &mach = u.machines[0];
  if (mach.second.at("TUXCONFIG") != std::getenv("TUXCONFIG")) {
    throw std::logic_error(
        "TUXCONFIG in configuration file and environment do not match");
  }
  checked_copy(mach.first, m.mach().address);
  checked_copy(mach.second.at("LMID"), m.mach().lmid);
  checked_copy(mach.second.at("TUXCONFIG"), m.mach().tuxconfig);
  checked_copy(mach.second.at("TUXDIR"), m.mach().tuxdir);
  checked_copy(mach.second.at("APPDIR"), m.mach().appdir);
  checked_copy(mach.second["ULOGPFX"], m.mach().ulogpfx);

  std::map<std::string, uint16_t> group_ids;
  for (auto &grpconf : u.groups) {
    group_ids[grpconf.first] = checked_get(grpconf.second, "GRPNO", 1, 30000);
  }

  auto servers = m.servers();
  for (auto &srvconf : u.servers) {
    auto basesrvid = std::stoi(srvconf.second["SRVID"]);
    auto min = checked_get(srvconf.second, "MIN", 1, 1000, 1);
    auto max = checked_get(srvconf.second, "MAX", 1, 1000, 1);
    auto grpno = group_ids.at(srvconf.second.at("SRVGRP"));
    for (auto n = 0; n < max; n++) {
      auto srvid = basesrvid + n;
      auto rqaddr = srvconf.second["RQADDR"];
      if (rqaddr.empty()) {
        rqaddr = "." + std::to_string(srvid);
      }

      auto &server = servers.at(m.make_server(srvid, grpno, srvconf.first,
                                              srvconf.second["CLOPT"], rqaddr));
      server.autostart = n < min;
    }
  }
}
