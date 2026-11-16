// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdlib>
#include <algorithm>

#include "cfg.h"
#include "misc.h"

namespace fux::cfg {

namespace {

long to_long(const std::string &value, const std::string &key, long min,
             long max, long defvalue) {
  if (value.empty()) {
    return defvalue;
  }
  try {
    auto n = std::stol(value);
    if (n < min || n > max) {
      throw std::out_of_range(key + " out of range");
    }
    return n;
  } catch (const std::invalid_argument &) {
    throw std::out_of_range(key + " is not a number: " + value);
  }
}

std::string param(const ubb_params &params, const std::string &key) {
  auto it = params.find(key);
  return it == params.end() ? "" : it->second;
}

}  // namespace

tables build(const ubbconfig &ubb) {
  tables t;

  auto ipckey = ubb.resources.find("IPCKEY");
  if (ipckey == ubb.resources.end()) {
    throw std::out_of_range("IPCKEY required");
  }
  try {
    t.dom.ipckey = std::stoul(ipckey->second);
  } catch (const std::invalid_argument &) {
    throw std::out_of_range("IPCKEY is not a number: " + ipckey->second);
  }

  t.dom.master = param(ubb.resources, "MASTER");
  t.dom.model = param(ubb.resources, "MODEL");
  if (t.dom.model.empty()) {
    t.dom.model = "SHM";
  }
  t.dom.domainid = param(ubb.resources, "DOMAINID");
  t.dom.ldbal = param(ubb.resources, "LDBAL");
  if (t.dom.ldbal.empty()) {
    t.dom.ldbal = "N";
  }
  t.dom.maxservers = to_long(param(ubb.resources, "MAXSERVERS"),
                                "MAXSERVERS", 1, 8192, 50);
  t.dom.maxservices = to_long(param(ubb.resources, "MAXSERVICES"),
                                 "MAXSERVICES", 1, 1048575, 100);
  t.dom.maxgroups =
      to_long(param(ubb.resources, "MAXGROUPS"), "MAXGROUPS", 1, 32768, 100);
  t.dom.maxqueues =
      to_long(param(ubb.resources, "MAXQUEUES"), "MAXQUEUES", 1, 8192, 50);
  t.dom.maxaccessers = to_long(param(ubb.resources, "MAXACCESSERS"),
                                  "MAXACCESSERS", 1, 32768, 50);

  if (ubb.machines.size() != 1) {
    throw std::logic_error("Exactly one MACHINE entry supported");
  }
  auto &mach = ubb.machines[0];
  t.mach.pmid = mach.first;
  t.mach.lmid = param(mach.second, "LMID");
  t.mach.appdir = param(mach.second, "APPDIR");
  t.mach.tuxconfig = param(mach.second, "TUXCONFIG");
  t.mach.tuxdir = param(mach.second, "TUXDIR");
  t.mach.ulogpfx = param(mach.second, "ULOGPFX");
  t.mach.tlogdevice = param(mach.second, "TLOGDEVICE");

  std::string blocktime = param(ubb.resources, "BLOCKTIME");
  if (blocktime.empty()) {
    blocktime = "15";
  }
  std::string scanunit = param(ubb.resources, "SCANUNIT");
  if (scanunit.empty()) {
    scanunit = "1000MS";
  }
  std::transform(scanunit.begin(), scanunit.end(), scanunit.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  long scan_ms;
  if (scanunit.size() > 2 && scanunit.compare(scanunit.size() - 2, 2, "ms") == 0) {
    scan_ms = std::stol(scanunit.substr(0, scanunit.size() - 2));
  } else {
    scan_ms = std::stol(scanunit) * 1000;
  }
  t.mach.blocktime = std::stol(blocktime) * scan_ms;

  for (auto &grpconf : ubb.groups) {
    group g;
    g.srvgrp = grpconf.first;
    g.grpno = to_long(param(grpconf.second, "GRPNO"), "GRPNO", 1, 30000, 0);
    if (g.grpno == 0) {
      throw std::out_of_range("GRPNO required");
    }
    g.lmid = param(grpconf.second, "LMID");
    g.openinfo = param(grpconf.second, "OPENINFO");
    g.closeinfo = param(grpconf.second, "CLOSEINFO");
    g.tmsname = param(grpconf.second, "TMSNAME");
    t.groups.push_back(std::move(g));
  }

  for (auto &srvconf : ubb.servers) {
    server s;
    s.servername = srvconf.first;
    s.srvgrp = param(srvconf.second, "SRVGRP");
    s.srvid = to_long(param(srvconf.second, "SRVID"), "SRVID", 1, 30000, 0);
    if (s.srvid == 0) {
      throw std::out_of_range("SRVID required");
    }
    s.min = to_long(param(srvconf.second, "MIN"), "MIN", 1, 1000, 1);
    s.max = to_long(param(srvconf.second, "MAX"), "MAX", 1, 1000, 1);
    s.mindispatchthreads = to_long(param(srvconf.second, "MINDISPATCHTHREADS"),
                                   "MINDISPATCHTHREADS", 1, 1000, 1);
    s.maxdispatchthreads = to_long(param(srvconf.second, "MAXDISPATCHTHREADS"),
                                   "MAXDISPATCHTHREADS", 1, 1000, 1);
    s.clopt = param(srvconf.second, "CLOPT");
    if (s.clopt.empty()) {
      s.clopt = "-A";
    }
    s.envfile = param(srvconf.second, "ENVFILE");
    s.dependson = param(srvconf.second, "DEPENDSON");
    s.rcmd = param(srvconf.second, "RCMD");
    s.rqaddr = param(srvconf.second, "RQADDR");
    s.grace = to_long(param(srvconf.second, "GRACE"), "GRACE", 0, 86400, 86400);
    s.maxgen = to_long(param(srvconf.second, "MAXGEN"), "MAXGEN", 1, 1000, 1);
    s.threadstacksize =
        to_long(param(srvconf.second, "THREADSTACKSIZE"), "THREADSTACKSIZE", 0,
                100000000, 0);
    s.restart = param(srvconf.second, "RESTART") == "Y";
    s.replyq = param(srvconf.second, "REPLYQ") != "N";
    s.secondaryrq = param(srvconf.second, "SECONDARYRQ") == "Y";
    auto grp = std::find_if(t.groups.begin(), t.groups.end(), [&](auto &g) {
      return g.srvgrp == s.srvgrp;
    });
    if (s.srvgrp.empty() || grp == t.groups.end()) {
      throw std::out_of_range("Unknown SRVGRP " + s.srvgrp);
    }
    if (s.rqaddr.empty()) {
      s.rqaddr = std::to_string(grp->grpno) + "." + std::to_string(s.srvid);
    }
    t.servers.push_back(std::move(s));
  }

  return t;
}

std::string emit(const tables &t) {
  std::string out;

  out += "*RESOURCES\n";
  out += "IPCKEY " + std::to_string(t.dom.ipckey) + "\n";
  if (!t.dom.master.empty()) {
    out += "MASTER " + t.dom.master + "\n";
  }
  out += "MODEL " + t.dom.model + "\n";
  if (!t.dom.domainid.empty()) {
    out += "DOMAINID " + t.dom.domainid + "\n";
  }
  out += "LDBAL " + t.dom.ldbal + "\n";
  out += "MAXACCESSERS " + std::to_string(t.dom.maxaccessers) + "\n";
  out += "MAXSERVERS " + std::to_string(t.dom.maxservers) + "\n";
  out += "MAXSERVICES " + std::to_string(t.dom.maxservices) + "\n";
  out += "MAXGROUPS " + std::to_string(t.dom.maxgroups) + "\n";
  out += "MAXQUEUES " + std::to_string(t.dom.maxqueues) + "\n";

  out += "\n*MACHINES\n";
  out += "\"" + t.mach.pmid + "\" LMID=" + t.mach.lmid;
  if (!t.mach.appdir.empty()) {
    out += " APPDIR=\"" + t.mach.appdir + "\"";
  }
  if (!t.mach.tuxconfig.empty()) {
    out += " TUXCONFIG=\"" + t.mach.tuxconfig + "\"";
  }
  if (!t.mach.tuxdir.empty()) {
    out += " TUXDIR=\"" + t.mach.tuxdir + "\"";
  }
  if (!t.mach.ulogpfx.empty()) {
    out += " ULOGPFX=\"" + t.mach.ulogpfx + "\"";
  }
  if (!t.mach.tlogdevice.empty()) {
    out += " TLOGDEVICE=\"" + t.mach.tlogdevice + "\"";
  }
  out += "\n";

  if (!t.groups.empty()) {
    out += "\n*GROUPS\n";
    for (auto &g : t.groups) {
      out += g.srvgrp + " LMID=" + g.lmid + " GRPNO=" + std::to_string(g.grpno);
      if (!g.openinfo.empty()) {
        out += " OPENINFO=\"" + g.openinfo + "\"";
      }
      if (!g.closeinfo.empty()) {
        out += " CLOSEINFO=\"" + g.closeinfo + "\"";
      }
      if (!g.tmsname.empty()) {
        out += " TMSNAME=\"" + g.tmsname + "\"";
      }
      out += "\n";
    }
  }

  if (!t.servers.empty()) {
    out += "\n*SERVERS\n";
    for (auto &s : t.servers) {
      out += s.servername + " SRVGRP=" + s.srvgrp + " SRVID=" +
             std::to_string(s.srvid) + " MIN=" + std::to_string(s.min) +
             " MAX=" + std::to_string(s.max) + " MINDISPATCHTHREADS=" +
             std::to_string(s.mindispatchthreads) + " MAXDISPATCHTHREADS=" +
             std::to_string(s.maxdispatchthreads) + " CLOPT=\"" + s.clopt +
             "\" RQADDR=" + s.rqaddr + " REPLYQ=" +
             (s.replyq ? "Y" : "N") + " MAXGEN=" + std::to_string(s.maxgen) +
             " RESTART=" + (s.restart ? "Y" : "N") +
             " GRACE=" + std::to_string(s.grace) +
             " SECONDARYRQ=" + (s.secondaryrq ? "Y" : "N");
      if (!s.envfile.empty()) {
        out += " ENVFILE=\"" + s.envfile + "\"";
      }
      if (!s.dependson.empty()) {
        out += " DEPENDSON=\"" + s.dependson + "\"";
      }
      if (!s.rcmd.empty()) {
        out += " RCMD=\"" + s.rcmd + "\"";
      }
      if (s.threadstacksize != 0) {
        out += " THREADSTACKSIZE=" + std::to_string(s.threadstacksize);
      }
      out += "\n";
    }
  }

  return out;
}

}  // namespace fux::cfg
