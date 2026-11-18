// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// dmadmin: domains administration. Talks to each local access point's
// gateway through its DMADM_<accesspoint> service for connection state
// queries and connect/disconnect requests. Configuration is read from
// the BDMCONFIG named by the BDMCONFIG environment variable; the
// application must be running.

#include <unistd.h>

#include <atmi.h>
#include <fml32.h>
#include <userlog.h>
#include <xatmi.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "dmcfg.h"
#include "dmib.h"
#include "fux.h"
#include "misc.h"

namespace {

fux::cfg::dmconfig dm;

struct row {
  std::string lap;
  std::string ap;
  std::string apid;
  std::string nwaddr;
  std::string policy;
  std::string state;
};

// One admin request against a local access point's gateway; throws
// xatmi_error when the gateway is not reachable or refuses the call.
// The request buffer doubles as the reply buffer, like a plain tpcall.
std::vector<row> query(const std::string &lap, const std::string &op,
                       const std::string &ap) {
  fux::fml32buf rq;
  rq.put(dmib::operation(), 0, op);
  if (!ap.empty()) {
    rq.put(dmib::accesspoint(), 0, ap);
  }
  fux::tpcall("DMADM_" + lap, rq, 0);
  std::vector<row> out;
  FLDOCC32 n = Foccur32(rq.ptr(), dmib::accesspoint());
  for (FLDOCC32 i = 0; i < n; i++) {
    row r;
    r.lap = lap;
    r.ap = rq.get<std::string>(dmib::accesspoint(), i, "");
    r.apid = rq.get<std::string>(dmib::accesspointid(), i, "");
    r.nwaddr = rq.get<std::string>(dmib::nwaddr(), i, "");
    r.policy = rq.get<std::string>(dmib::policy(), i, "");
    r.state = rq.get<std::string>(dmib::state(), i, "");
    out.push_back(r);
  }
  return out;
}

std::vector<row> all_gateways(const std::string &op, const std::string &ap) {
  std::vector<row> out;
  for (auto &l : dm.locals) {
    try {
      auto rows = query(l.accesspoint, op, ap);
      out.insert(out.end(), rows.begin(), rows.end());
    } catch (const fux::xatmi_error &) {
      row r;
      r.lap = l.accesspoint;
      r.state = "UNREACHABLE";
      out.push_back(r);
    }
  }
  return out;
}

void print_rows(const std::vector<row> &rows) {
  for (auto &r : rows) {
    if (r.state == "UNREACHABLE") {
      std::cout << "  gateway for " << r.lap << " not reachable"
                << std::endl;
    } else {
      std::cout << "  " << r.ap << "  " << r.apid << "  " << r.nwaddr
                << "  " << r.policy << "  " << r.state << std::endl;
    }
  }
}

std::vector<std::string> split(const std::string &line) {
  std::istringstream in(line);
  std::vector<std::string> out;
  std::string word;
  while (in >> word) {
    out.push_back(word);
  }
  return out;
}

std::string default_lap;
bool verbose_on = false;
bool paginate_on = false;

void help() {
  std::cout
      << "commands:\n"
      << "  printdomains (pd)     local access points and their remotes\n"
      << "  printconn [apid]      connection state (optionally one remote)\n"
      << "  connect (co) <apid>   open the connection to a remote\n"
      << "  disconnect (dco) <apid>  close the connection to a remote\n"
      << "  default (d) [domain]  show or set the default local domain\n"
      << "  echo (e)              accepted for compatibility\n"
      << "  verbose (v) [off]     accepted for compatibility\n"
      << "  paginate (page) [off] accepted for compatibility\n"
      << "  help (h)              this text\n"
      << "  quit (q)              leave dmadmin\n";
}

int run_command(const std::vector<std::string> &args) {
  if (args.empty()) {
    return 0;
  }
  const std::string &cmd = args[0];
  if (cmd == "quit" || cmd == "exit" || cmd == "q") {
    return 1;
  }
  if (cmd == "help" || cmd == "h") {
    help();
    return 0;
  }
  if (cmd == "echo" || cmd == "e") {
    // accepted for compatibility; the REPL always echoes nothing extra
    return 0;
  }
  if (cmd == "verbose" || cmd == "v") {
    verbose_on = args.size() < 2 || args[1] != "off";
    std::cout << "verbose " << (verbose_on ? "on" : "off") << std::endl;
    return 0;
  }
  if (cmd == "paginate" || cmd == "page") {
    paginate_on = args.size() < 2 || args[1] != "off";
    std::cout << "paginate " << (paginate_on ? "on" : "off") << std::endl;
    return 0;
  }
  if (cmd == "default" || cmd == "d") {
    if (args.size() > 1) {
      bool known = false;
      for (auto &l : dm.locals) {
        if (l.accesspoint == args[1]) {
          known = true;
        }
      }
      if (!known) {
        std::cerr << "default: unknown local domain " << args[1] << std::endl;
        return 0;
      }
      default_lap = args[1];
    }
    std::cout << (default_lap.empty() ? "(none)" : default_lap) << std::endl;
    return 0;
  }
  try {
    if (cmd == "printdomains" || cmd == "pd") {
      for (auto &l : dm.locals) {
        const fux::cfg::dm_tdomain_addr *ta = nullptr;
        for (auto &t : dm.tdomain) {
          if (t.accesspoint == l.accesspoint) {
            ta = &t;
          }
        }
        std::cout << l.accesspoint << "  " << l.accesspointid << "  "
                  << "group " << l.gwgrp;
        if (ta != nullptr) {
          std::cout << "  " << ta->nwaddr;
        }
        std::cout << std::endl;
        auto rows = query(l.accesspoint, "GET", "");
        print_rows(rows);
      }
      return 0;
    }
    if (cmd == "printconn") {
      std::string ap = args.size() > 1 ? args[1] : "";
      print_rows(all_gateways("GET", ap));
      return 0;
    }
    if (cmd == "connect" || cmd == "co" || cmd == "disconnect" ||
        cmd == "dco") {
      if (args.size() < 2) {
        std::cerr << cmd << ": access point name required" << std::endl;
        return 0;
      }
      const char *known = nullptr;
      for (auto &r : dm.remotes) {
        if (r.accesspoint == args[1] || r.accesspointid == args[1]) {
          known = r.accesspoint.c_str();
        }
      }
      if (known == nullptr) {
        std::cerr << cmd << ": unknown access point " << args[1]
                  << std::endl;
        return 0;
      }
      auto rows = all_gateways(
          (cmd == "connect" || cmd == "co") ? "CONNECT" : "DISCONNECT",
          known);
      for (auto &r : rows) {
        if (r.state != "UNREACHABLE") {
          std::cout << (cmd == "co" ? "connect" : cmd) << " " << known << ": "
                  << r.state << std::endl;
        }
      }
      return 0;
    }
    std::cerr << "unknown command " << cmd << " (try help)" << std::endl;
  } catch (const fux::xatmi_error &e) {
    std::cerr << cmd << ": " << e.what() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << cmd << ": " << e.what() << std::endl;
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  auto path = fux::util::getenv("BDMCONFIG", "");
  if (path.empty()) {
    std::cerr << "dmadmin: BDMCONFIG environment variable not set"
              << std::endl;
    return 1;
  }
  try {
    dm = fux::cfg::dm_load_file(path);
  } catch (const std::exception &e) {
    std::cerr << "dmadmin: " << e.what() << std::endl;
    return 1;
  }
  if (dm.locals.empty()) {
    std::cerr << "dmadmin: no local access points in " << path << std::endl;
    return 1;
  }

  bool interactive = isatty(fileno(stdin));
  std::string line;
  while (true) {
    if (interactive) {
      std::cout << "dmadmin> " << std::flush;
    }
    if (!std::getline(std::cin, line)) {
      break;
    }
    if (run_command(split(line)) != 0) {
      break;
    }
  }
  return 0;
}
