// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef HAVE_CXX_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <tpadm.h>
#include <userlog.h>

#include "fux.h"
#include "config.h"
#include "ipc.h"

#include "mib.h"
#include "tlog.h"

static void do_help() {
  std::cout << "help (h) [{command | all}]" << std::endl;
  std::cout << "printservice (psc) [service]" << std::endl;
  std::cout << "printqueue (pq) [qaddress]" << std::endl;
  std::cout << "printserver (psr) [-m machine] [-g groupname [-R rmid]] [-i "
               "srvid] [-q qaddress]"
            << std::endl;
  std::cout << "changetrace [-m machine] [-g groupname] [-i srvid] "
               "[tracelevel | off]"
            << std::endl;
  std::cout << "advertise (adv) [-g groupname] [-i srvid] service"
            << std::endl;
  std::cout << "unadvertise (unadv) [-g groupname] [-i srvid] service"
            << std::endl;
  std::cout << "suspend [-g groupname] [-i srvid]" << std::endl;
  std::cout << "resume [-g groupname] [-i srvid]" << std::endl;
  std::cout << "printserver (psr) [-g groupname] [-i srvid]" << std::endl;
  std::cout << "crdl [-b blocks] -z device" << std::endl;
  std::cout << "crlog -m machine" << std::endl;
}

// Shared option parsing for the server-targeting commands.
struct target_opts {
  std::string grpname;
  int srvid = -1;
  std::vector<std::string> positional;
};

static target_opts parse_targets(const std::string &args) {
  target_opts t;
  std::istringstream in(args);
  std::string word;
  while (in >> word) {
    if (word == "-g") {
      in >> t.grpname;
    } else if (word == "-i") {
      in >> t.srvid;
    } else if (word == "-m") {
      std::string m;
      in >> m;  // accepted for changetrace compatibility, ignored here
    } else {
      t.positional.push_back(word);
    }
  }
  return t;
}

// The bulletin board must be up and the BBL alive before any runtime
// mutation, the way the platform guards its admin path.
static mib &require_bb() {
  mib &m = getmib();
  auto bbl = m.find_server(0, 0);
  if (bbl == mib::badoff || !alive(m.servers().at(bbl).pid)) {
    throw std::runtime_error("bulletin board is not active");
  }
  return m;
}

static size_t find_target_server(mib &m, const target_opts &t) {
  size_t grpfilter = mib::badoff;
  if (!t.grpname.empty()) {
    grpfilter = m.group_index(t.grpname);
    if (grpfilter == mib::badoff) {
      throw std::runtime_error("group " + t.grpname + " not found");
    }
  }
  for (size_t i = 0; i < m.servers().length(); i++) {
    auto &s = m.servers().at(i);
    if (t.srvid != -1 && s.srvid != t.srvid) {
      continue;
    }
    if (t.srvid == -1 && s.grpno == 0) {
      continue;  // need an explicit target; never default to the BBL
    }
    if (grpfilter != mib::badoff && s.group_idx != grpfilter) {
      continue;
    }
    return i;
  }
  throw std::runtime_error("no such server");
}

static void do_advertise(const std::string &args, bool advertise) {
  target_opts t = parse_targets(args);
  if (t.positional.empty()) {
    std::cerr << (advertise ? "adv" : "unadv") << ": service name required"
              << std::endl;
    return;
  }
  mib &m = require_bb();
  size_t srv = find_target_server(m, t);
  auto &server = m.servers().at(srv);
  if (server.state != state_t::ACTive || !alive(server.pid)) {
    std::cerr << (advertise ? "adv" : "unadv")
              << ": server is not active" << std::endl;
    return;
  }
  size_t flipped =
      m.set_service_advertised(t.positional[0], srv, advertise);
  if (flipped == 0) {
    std::cerr << (advertise ? "adv" : "unadv") << ": service "
              << t.positional[0] << " not known on this server" << std::endl;
    return;
  }
  userlog("%s %s on -g %s -i %d", advertise ? "adv" : "unadv",
          t.positional[0].c_str(), t.grpname.c_str(), server.srvid);
  std::cout << "\tService " << t.positional[0]
            << (advertise ? " advertised" : " unadvertised") << std::endl;
}

static void do_suspend(const std::string &args, bool suspend) {
  target_opts t = parse_targets(args);
  mib &m = require_bb();
  size_t srv = find_target_server(m, t);
  auto &server = m.servers().at(srv);
  if (server.state != state_t::ACTive || !alive(server.pid)) {
    std::cerr << (suspend ? "suspend" : "resume") << ": no such server"
              << std::endl;
    return;
  }
  size_t flipped = m.set_server_advertisements_active(srv, !suspend);
  userlog("%s -g %s -i %d (%zu services)", suspend ? "suspend" : "resume",
          t.grpname.c_str(), server.srvid, flipped);
  std::cout << "\tServer Id = " << server.srvid << " Group Id = "
            << server.grpno << ":  " << (suspend ? "suspended" : "resumed")
            << std::endl;
}

static void do_printserver() {
  fux::fml32buf in, out;
  in.put(TA_CLASS, 0, "T_SERVER");
  in.put(TA_OPERATION, 0, "GET");
  tpadmcall(in.ptr(), out.ptrptr(), 0);

  std::cout << "Prog Name      Queue Name  Grp Name      ID State"
            << std::endl;
  std::cout << "---------      ----------  --------      -- -----" << std::endl;
  for (FLDOCC32 oc = 0, max = out.get<long>(TA_OCCURS, 0); oc < max; oc++) {
    std::cout << std::left << std::setw(14)
              << out.get<std::string>(TA_SERVERNAME, oc) << " " << std::left
              << std::setw(11) << out.get<std::string>(TA_RQADDR, oc) << " "
              << std::left << std::setw(12)
              << out.get<std::string>(TA_SRVGRP, oc) << " " << std::right
              << std::setw(4) << out.get<long>(TA_SRVID, oc) << " "
              << out.get<std::string>(TA_STATE, oc) << std::endl;
  }
}

static void do_printservice(const std::string &args) {
  std::istringstream in_args(args);
  std::string svc;
  in_args >> svc;

  fux::fml32buf in, out;
  in.put(TA_CLASS, 0, "T_SERVICE");
  in.put(TA_OPERATION, 0, "GET");
  if (!svc.empty()) {
    in.put(TA_SERVICENAME, 0, svc);
  }
  tpadmcall(in.ptr(), out.ptrptr(), 0);

  std::cout << "Service Name   Routine Name  Prog Name      Grp Name    "
               "ID  Machine    # Done  # Queued  Status"
            << std::endl;
  std::cout << "------------   ------------  ---------      --------    "
               "--  -------    ------  --------  ------"
            << std::endl;
  for (FLDOCC32 oc = 0, max = out.get<long>(TA_OCCURS, 0); oc < max; oc++) {
    std::cout << std::left << std::setw(14)
              << out.get<std::string>(TA_SERVICENAME, oc) << " "
              << std::left << std::setw(12)
              << out.get<std::string>(TA_SERVICENAME, oc) << " " << std::left
              << std::setw(13)
              << out.get<std::string>(TA_SERVERNAME, oc, "") << " "
              << std::left << std::setw(11)
              << out.get<std::string>(TA_SRVGRP, oc, "") << " " << std::right
              << std::setw(4) << out.get<long>(TA_SRVID, oc, 0L) << "  "
              << std::left << std::setw(9) << "" << " " << std::right
              << std::setw(6) << 0 << " " << std::right << std::setw(9) << 0
              << "  " << out.get<std::string>(TA_STATE, oc, "") << std::endl;
  }
}

static void do_crdl(const std::string &args) {
  std::istringstream in(args);
  std::string word, device;
  uint64_t blocks = 100;
  while (in >> word) {
    if (word == "-b") {
      in >> blocks;
    } else if (word == "-z") {
      in >> device;
    } else if (word == "-o") {
      uint64_t ignored;
      in >> ignored;  // offset is emulated, kept for CLI compatibility
    }
  }
  if (device.empty()) {
    std::cerr << "crdl: -z device is required" << std::endl;
    return;
  }
  fux::tlog::log::create(device, 0, blocks);
  std::cout << "Created transaction log " << device << " (" << blocks
            << " pages)" << std::endl;
}

static void do_crlog(const std::string &args) {
  std::istringstream in(args);
  std::string word, lmid;
  while (in >> word) {
    if (word == "-m") {
      in >> lmid;
    }
  }
  mib &m = getmib();
  if (!lmid.empty() && lmid != m.mach().lmid) {
    std::cerr << "crlog: machine " << lmid << " not found" << std::endl;
    return;
  }
  std::string device = m.mach().tlogdevice;
  if (device.empty()) {
    std::cerr << "crlog: TLOGDEVICE is not configured" << std::endl;
    return;
  }
  std::error_code ec;
  if (!fs::exists(fs::path(device) / ".tlog", ec)) {
    fux::tlog::log::create(device, 0, 100);
  }
  auto log = fux::tlog::log::open(device);
  std::cout << "TLOG " << device << " ready, " << log.size() << " pages"
            << std::endl;
}

static bool g_readonly = false;

static bool refuse_readonly(const char *cmd) {
  if (g_readonly) {
    std::cerr << cmd << ": not allowed in read-only mode" << std::endl;
    return true;
  }
  return false;
}

// changetrace: dye-trace level per server, stored in the bulletin board
// and honored by the dispatch loop (one ULOG line per call). Matches the
// platform command surface; the level string is kept verbatim.
static void do_changetrace(const std::string &args) {
  std::string grpname, srvid_s, machine, spec;
  std::istringstream in(args);
  std::string word;
  while (in >> word) {
    if (word == "-g") {
      in >> grpname;
    } else if (word == "-i") {
      in >> srvid_s;
    } else if (word == "-m") {
      in >> machine;
    } else {
      spec = word;
    }
  }
  if (spec.empty()) {
    std::cerr << "changetrace: tracelevel or off required" << std::endl;
    return;
  }
  if (!machine.empty() && machine != "all") {
    mib &m = getmib();
    if (machine != m.mach().lmid) {
      std::cerr << "changetrace: machine " << machine << " not found"
                << std::endl;
      return;
    }
  }
  int srvid = srvid_s.empty() ? -1 : std::stoi(srvid_s);
  const char *level = spec == "off" ? "" : spec.c_str();

  mib &m = getmib();
  auto bbl = m.find_server(0, 0);
  if (bbl == mib::badoff || !alive(m.servers().at(bbl).pid)) {
    std::cerr << "changetrace: bulletin board is not active" << std::endl;
    return;
  }

  size_t grpfilter = mib::badoff;
  if (!grpname.empty()) {
    grpfilter = m.group_index(grpname);
    if (grpfilter == mib::badoff) {
      std::cerr << "changetrace: group " << grpname << " not found"
                << std::endl;
      return;
    }
  }

  auto servers = m.servers();
  size_t changed = 0, inactive = 0;
  for (size_t i = 0; i < servers.length(); i++) {
    auto &s = servers.at(i);
    if (s.pid == 0) {
      continue;
    }
    if (srvid != -1 && s.srvid != srvid) {
      continue;
    }
    if (grpfilter != mib::badoff && s.group_idx != grpfilter) {
      continue;
    }
    if (s.state != state_t::ACTive) {
      inactive++;
      continue;
    }
    checked_copy(level, s.tracelvl);
    userlog("changetrace -g %s -i %d: trace level set to \"%s\"",
            grpname.empty() ? "-" : grpname.c_str(), s.srvid,
            level[0] == '\0' ? "off" : level);
    std::cout << "\tServer Id = " << s.srvid << " Group Id = " << s.grpno
              << ":  trace level changed" << std::endl;
    changed++;
  }
  if (changed == 0) {
    std::cerr << "changetrace: no active servers matched"
              << (inactive ? " (target server is not active)" : "")
              << std::endl;
  }
}

static void do_printqueue() {
  fux::fml32buf in, out;
  in.put(TA_CLASS, 0, "T_QUEUE");
  in.put(TA_OPERATION, 0, "GET");

  tpadmcall(in.ptr(), out.ptrptr(), 0);

  std::cout << "Prog Name      Queue Name  # Serve Wk Queued  # Queued  Ave. "
               "Len    Machine"
            << std::endl;
  std::cout << "---------      ------------------- ---------  --------  "
               "--------    -------"
            << std::endl;

  for (FLDOCC32 oc = 0, max = out.get<long>(TA_OCCURS, 0); oc < max; oc++) {
    std::cout << std::left << std::setw(14)
              << out.get<std::string>(TA_SERVERNAME, oc) << " " << std::left
              << std::setw(9) << out.get<std::string>(TA_RQADDR, oc) << " "
              << std::right << std::setw(9) << out.get<long>(TA_SERVERCNT, oc)
              << " " << std::right << std::setw(9)
              << out.get<long>(TA_WKQUEUED, oc) << " " << std::right
              << std::setw(9) << out.get<long>(TA_NQUEUED, oc) << " "
              << std::right << std::setw(9) << 0 << "     " << std::left
              << std::setw(8) << out.get<std::string>(TA_LMID, oc) << std::endl;
  }
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool version = false;

  auto parser =
      clara::Help(show_help) | clara::Opt(version)["-v"]("show version info") |
      clara::Opt(g_readonly)["-r"]("attach in read-only mode");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  if (show_help) {
    std::cout << parser;
    return 0;
  }

  try {
    if (version) {
      std::cout << "INFO: " << PACKAGE_NAME << " " << VERSION << std::endl;
      return 0;
    }

    if (g_readonly) {
      std::cout << "tmadmin: cannot become administrator, limited to "
                   "read-only commands"
                << std::endl;
    }

    std::string line;
    std::cout << "> " << std::flush;
    while (getline(std::cin, line)) {
      if (line.empty()) {
        std::cout << "> " << std::flush;
        continue;
      }
      // every command is audit-logged, like the platform's admin trail
      userlog("Command: %s", line.c_str());
      try {
        if (line == "help" || line == "h") {
          do_help();
        } else if (line == "printqueue" || line == "pq") {
          do_printqueue();
        } else if (line == "printserver" || line == "psr") {
          do_printserver();
        } else if (line == "printservice" || line == "psc") {
          do_printservice("");
        } else if (line.rfind("printservice", 0) == 0) {
          do_printservice(line.substr(strlen("printservice")));
        } else if (line.rfind("psc", 0) == 0) {
          do_printservice(line.substr(strlen("psc")));
        } else if (line.rfind("changetrace", 0) == 0) {
          if (!refuse_readonly("changetrace")) {
            do_changetrace(line.substr(strlen("changetrace")));
          }
        } else if (line.rfind("unadv", 0) == 0) {
          if (!refuse_readonly("unadv")) {
            do_advertise(line.substr(strlen("unadv")), false);
          }
        } else if (line.rfind("adv", 0) == 0) {
          if (!refuse_readonly("adv")) {
            do_advertise(line.substr(strlen("adv")), true);
          }
        } else if (line.rfind("suspend", 0) == 0) {
          if (!refuse_readonly("suspend")) {
            do_suspend(line.substr(strlen("suspend")), true);
          }
        } else if (line.rfind("resume", 0) == 0) {
          if (!refuse_readonly("resume")) {
            do_suspend(line.substr(strlen("resume")), false);
          }
        } else if (line.rfind("crdl", 0) == 0) {
          if (!refuse_readonly("crdl")) {
            do_crdl(line.substr(strlen("crdl")));
          }
        } else if (line.rfind("crlog", 0) == 0) {
          if (!refuse_readonly("crlog")) {
            do_crlog(line.substr(strlen("crlog")));
          }
        } else if (line == "quit" || line == "q") {
          break;
        } else {
          std::cerr << line << ": invalid command" << std::endl;
        }
      } catch (const std::exception &e) {
        std::string cmd = line.substr(0, line.find(' '));
        std::cerr << cmd << ": " << e.what() << std::endl;
      }
      std::cout << "> " << std::flush;
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
