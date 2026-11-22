// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdlib>
#include <functional>  //std::hash
#include <iostream>

#include <fml32.h>

#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "routing.h"
#include "ubb2mib.h"
#include "ubbreader.h"
#include "view32.h"

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
    throw std::logic_error("Exactly one MACHINE entry supported");
  }
  auto &mach = u.machines[0];
  if (mach.second.at("TUXCONFIG") != std::getenv("TUXCONFIG")) {
    throw std::logic_error(
        "TUXCONFIG in configuration file and environment do not match");
  }

  srand(time(nullptr));
  m->host = std::hash<std::string>()(mach.first);
  m->counter = rand();

  checked_copy(mach.first, m.mach().address);
  checked_copy(mach.second.at("LMID"), m.mach().lmid);
  checked_copy(mach.second.at("TUXCONFIG"), m.mach().tuxconfig);
  checked_copy(mach.second.at("TUXDIR"), m.mach().tuxdir);
  checked_copy(mach.second.at("APPDIR"), m.mach().appdir);
  checked_copy(mach.second["ULOGPFX"], m.mach().ulogpfx);
  checked_copy(mach.second["TLOGDEVICE"], m.mach().tlogdevice);
  checked_copy(mach.second["ENVFILE"], m.mach().envfile);

  std::string blocktime = u.resources["BLOCKTIME"];
  if (blocktime.empty()) {
    blocktime = "15";
  }
  std::string scanunit = u.resources["SCANUNIT"];
  if (scanunit.empty()) {
    scanunit = "1000MS";
  }

  // scanunit to milliseconds
  std::transform(scanunit.begin(), scanunit.end(), scanunit.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (scanunit.size() > 2 &&
      scanunit.compare(scanunit.size() - 2, 2, "ms") == 0) {
    scanunit = scanunit.substr(0, scanunit.size() - 2);
  } else {
    // plain value is seconds
    scanunit += "000";
  }

  m.mach().blocktime = std::stol(blocktime) * std::stol(scanunit);
  m.mach().maxwsclients = std::stol(mach.second["MAXWSCLIENTS"].empty()
                                        ? "0"
                                        : mach.second["MAXWSCLIENTS"]);

  auto &domain = m.domain();
  domain.ipckey = m.cfg().ipckey;
  domain.maxservers = m.cfg().maxservers;
  domain.maxservices = m.cfg().maxservices;
  domain.maxqueues = m.cfg().maxqueues;
  domain.maxgroups = m.cfg().maxgroups;
  domain.maxaccessers = m.cfg().maxaccessers;

  std::map<std::string, uint16_t> group_ids;
  auto groups = m.groups();
  auto servers = m.servers();
  auto services = m.services();

  fux::fml32buf in, out;

  {
    //    in.reinit();
    //    tpadmcall(in.ptr(), out.ptrptr(), TPNOFLAGS);
    auto &server = servers.at(m.make_server(0, 0, "BBL", "-A", ".BBL"));
    server.autostart = true;
  }

  for (auto &grpconf : u.groups) {
    auto grpno = checked_get(grpconf.second, "GRPNO", 1, 30000);

    groups.at(m.make_group(grpno, grpconf.first, grpconf.second["OPENINFO"],
                           grpconf.second["CLOSEINFO"],
                           grpconf.second["TMSNAME"]));
    group_ids[grpconf.first] = grpno;
    if (grpconf.second["TMSNAME"].empty()) {
      continue;
    }

    auto tmscount = checked_get(grpconf.second, "TMSCOUNT", 2, 10, 3);
    for (auto n = 0; n < tmscount; n++) {
      auto &server =
          servers.at(m.make_server(30001 + n, grpno, grpconf.second["TMSNAME"],
                                   "-A", std::to_string(grpno) + ".TM"));
      server.autostart = true;
    }
  }

  for (auto &srvconf : u.servers) {
    auto basesrvid = checked_get(srvconf.second, "SRVID", 1, 30000);
    auto min = checked_get(srvconf.second, "MIN", 1, 1000, 1);
    auto max = checked_get(srvconf.second, "MAX", 1, 1000, 1);
    auto minthreads =
        checked_get(srvconf.second, "MINDISPATCHTHREADS", 1, 1000, 1);
    auto maxthreads =
        checked_get(srvconf.second, "MAXDISPATCHTHREADS", 1, 1000, 1);
    auto grpno = group_ids.at(srvconf.second.at("SRVGRP"));
    for (auto n = 0; n < max; n++) {
      auto srvid = basesrvid + n;
      auto rqaddr = srvconf.second["RQADDR"];
      if (rqaddr.empty()) {
        rqaddr = std::to_string(grpno) + "." + std::to_string(srvid);
      }

      auto &server = servers.at(m.make_server(srvid, grpno, srvconf.first,
                                              srvconf.second["CLOPT"], rqaddr));
      server.autostart = n < min;
      server.mindispatchthreads = minthreads;
      server.maxdispatchthreads = maxthreads;
      server.replyq = srvconf.second["REPLYQ"] != "N";
      server.secondaryrq = srvconf.second["SECONDARYRQ"] == "Y";
      server.grace = checked_get(srvconf.second, "GRACE", 0, 86400, 86400);
      server.maxgen = checked_get(srvconf.second, "MAXGEN", 1, 1000, 1);
      server.restart = srvconf.second["RESTART"] == "Y";
    }
  }

  for (auto &svcconf : u.services) {
    auto service = m.make_service(svcconf.first);
    auto conv = svcconf.second.find("BUFTYPECONV");
    if (conv != svcconf.second.end() && conv->second != "NOCONVERT") {
      checked_copy(conv->second, services.at(service).buftypeconv);
    }
    auto autotran = svcconf.second.find("AUTOTRAN");
    if (autotran != svcconf.second.end()) {
      checked_copy(autotran->second == "Y" ? "Y" : "N",
                   services.at(service).autotran);
    }
    auto rtname = svcconf.second.find("ROUTING");
    if (rtname != svcconf.second.end()) {
      checked_copy(rtname->second, services.at(service).routingname);
    }
  }

  auto routings = m.routings();
  for (auto &rtconf : u.routing) {
    if (routings->len >= maxrouting) {
      throw std::logic_error("Too many *ROUTING entries, maximum " +
                             std::to_string(maxrouting));
    }
    routing_entry entry = {};
    checked_copy(rtconf.first, entry.name);
    checked_copy(rtconf.second["FIELD"], entry.field);
    checked_copy(rtconf.second["BUFTYPE"], entry.buftype);
    checked_copy(rtconf.second["RANGES"], entry.ranges);
    if (entry.field[0] == '\0' || entry.buftype[0] == '\0' ||
        entry.ranges[0] == '\0') {
      throw std::logic_error("ROUTING entry " + rtconf.first +
                             " requires FIELD, BUFTYPE and RANGES");
    }
    try {
      fux::routing::parse_ranges(entry.ranges);
    } catch (const std::runtime_error &) {
      throw std::logic_error("ROUTING entry " + rtconf.first +
                             " contains an invalid RANGES value");
    }
    // resolve the routing field now (the way the platform resolves it in
    // the bulletin board at boot) so clients evaluate routing by id or
    // struct offset. VIEW-buffer criteria resolve through the view
    // registry (the 16-bit env names, like the rest of the VIEW flavor)
    if (strncmp(entry.buftype, "VIEW", 4) == 0) {
      const char *vname = entry.buftype[4] == ':' ? entry.buftype + 5 : "";
      auto view = fux::view::get16(vname);
      if (view == nullptr) {
        throw std::logic_error("Cannot map VIEW buftype " +
                               std::string(entry.buftype) +
                               " for criteria " + rtconf.first);
      }
      bool found = false;
      for (auto &m : view->members) {
        if (m.cname == entry.field) {
          entry.view_off = m.off;
          entry.fldtype = m.type;
          entry.fieldid = m.fldid;
          found = true;
          break;
        }
      }
      if (!found) {
        throw std::logic_error("Cannot map view field " +
                               std::string(entry.field) +
                               " for criteria " + rtconf.first);
      }
    } else {
      auto fieldid = Fldid32(entry.field);
      if (fieldid == BADFLDID) {
        throw std::logic_error("Cannot map field name " +
                               std::string(entry.field) +
                               " to field identifier for criteria " +
                               rtconf.first);
      }
      entry.fieldid = fieldid;
      entry.fldtype = Fldtype32(fieldid);
    }
    auto &slot = routings.at(routings->len);
    slot = entry;
    routings->len++;
  }
}
