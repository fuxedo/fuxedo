// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>

#include "compat.h"
#include "mib.h"
#include "misc.h"


static std::vector<std::string> split_clopt(const std::string &clopt) {
  std::vector<std::string> tokens;
  std::string cur;
  bool inq = false;
  for (char c : clopt) {
    if (c == '"') {
      inq = !inq;
      continue;
    }
    if (::isspace(c) && !inq) {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) {
    tokens.push_back(cur);
  }
  return tokens;
}

static void start(server &srv, bool no, bool d1, bool &failed) {
  std::vector<std::string> clopt = split_clopt(srv.clopt);
  std::vector<const char *> argv;
  argv.push_back(srv.servername);
  argv.push_back("-g");
  auto grpno = std::to_string(srv.grpno);
  argv.push_back(grpno.c_str());
  argv.push_back("-i");
  auto srvid = std::to_string(srv.srvid);
  argv.push_back(srvid.c_str());
  std::vector<std::string> quoted;
  for (auto &tok : clopt) {
    quoted.push_back(tok);
  }
  for (auto &tok : quoted) {
    argv.push_back(tok.c_str());
  }

  if (d1) {
    std::cout << "exec " << join(argv, " ") << " :" << std::endl;
  } else {
    std::cout << "exec " << srv.servername << " " << srv.clopt << " :"
              << std::endl;
  }
  if (no) {
    return;
  }
  if (srv.pid != 0 && alive(srv.pid)) {
    std::cout << "\tINFO: Duplicate server." << std::endl;
    return;
  }

  srv.state = state_t::INActive;
  auto pid = fork();
  if (pid == 0) {
    argv.push_back(nullptr);

    if (freopen("/dev/null", "r", stdin) == nullptr ||
        freopen("stdout", "a", stdout) == nullptr ||
        freopen("stderr", "a", stderr) == nullptr) {
      exit(-1);
    }

    if (execvp(srv.servername, (char *const *)&argv[0]) == -1) {
      exit(-1);
    }
  } else if (pid > 0) {
    srv.pid = pid;
    for (;;) {
      if (srv.state == state_t::ACTive) {
        break;
      }
      if (int n = waitpid(pid, nullptr, WNOHANG); n == pid || n == -1) {
        break;
      }

      struct timespec req = {0, 100000000};
      if (nanosleep(&req, nullptr) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }
    if (alive(pid)) {
      std::cout << "\tprocess id=" << srv.pid << " ... Started." << std::endl;
    } else {
      srv.pid = 0;
      failed = true;
      std::cout << "\tFailed." << std::endl;
    }
  } else {
    failed = true;
  }
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool yes = false;
  bool no = false;
  std::string dlevel;
  std::string grpname;
  std::string srvname;
  std::string lmid;
  int srvid = -1;
  bool admin_only = false;
  bool tms_only = false;

  /*
  Usage: tmboot [-w(ait)] [-n(oexec)] [-q(uiet)] [-y] [-c(heck)] [-d1]
               [{-A | -B loc | -M}] [{-S | [-l lmid] | -s aout |
               [-g grpname | -i srvid | -g grpname -i srvid]]} [-m minsrvs]]
               [-o sequence-#] [-T group-name] [-E envlabel] [-e errcmd] [-t
  timeout]
     */
  auto parser =
      clara::Help(show_help) |
      clara::Opt(yes)["-y"]("answer Yes to all questions") |
      clara::Opt(no)["-n"](
          "the execution sequence is printed, but not performed") |
      clara::Opt(admin_only)["-A"](
          "boot only administrative servers (BBL, GWADM, DMADM)") |
      clara::Opt(tms_only)["-S"]("boot only the TMS servers") |
      clara::Opt(dlevel, "debug level")["-d"](
          "causes command line options to be printed on the standard output") |
      clara::Opt(srvid, "srvid")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpname, "grpname")["-g"]("server's SRVGRP in TUXCONFIG") |
      clara::Opt(srvname, "aout")["-s"]("boot only servers named aout") |
      clara::Opt(lmid, "lmid")["-l"]("boot only on the named LMID");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  bool d1 = !dlevel.empty() && dlevel != "0";

  try {
    mib &m = getmib();

    if (!lmid.empty() && lmid != m.mach().lmid) {
      std::cerr << "lmid " << lmid << " not defined in this configuration"
                << std::endl;
      return -1;
    }

    m.validate();

    // The machine ENVFILE exports variables into booted servers (VAR=VALUE
    // lines); TUXDIR/TUXCONFIG/APPDIR from the configuration always win.
    // PATH and LD_LIBRARY_PATH found here replace the inherited values
    // before the platform prefix below is applied.
    if (m.mach().envfile[0] != '\0') {
      std::ifstream env(m.mach().envfile);
      if (!env) {
        std::cerr << "tmboot: cannot open ENVFILE " << m.mach().envfile
                  << std::endl;
        return -1;
      }
      std::string line;
      while (std::getline(env, line)) {
        if (line.empty() || line[0] == '#') {
          continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
          continue;
        }
        auto name = line.substr(0, eq);
        auto value = line.substr(eq + 1);
        if (name == "TUXDIR" || name == "TUXCONFIG" || name == "APPDIR") {
          std::cerr << "tmboot: warning: ENVFILE cannot override " << name
                    << std::endl;
          continue;
        }
        setenv(name.c_str(), value.c_str(), 1);
      }
    }

    std::ostringstream path;
    path << m.mach().tuxdir << "/bin:";
    path << m.mach().appdir << ":";
    path << std::getenv("PATH");

    setenv("PATH", path.str().c_str(), 1);

    auto servers = m.servers();
    size_t grpfilter = mib::badoff;
    if (!grpname.empty()) {
      grpfilter = m.group_index(grpname);
      if (grpfilter == mib::badoff) {
        std::cerr << "group " << grpname << " not found" << std::endl;
        return -1;
      }
    }
    bool any_failed = false;
    for (size_t i = 0; i < servers->len; i++) {
      auto &srv = servers[i];
      if (!srv.autostart) {
        continue;
      }
      bool is_admin = strcmp(srv.servername, "BBL") == 0 ||
                      strcmp(srv.servername, "GWADM") == 0 ||
                      strcmp(srv.servername, "DMADM") == 0;
      bool is_tms = strncmp(srv.servername, "TMS", 3) == 0;
      if (admin_only && !is_admin) {
        continue;
      }
      if (tms_only && !is_tms) {
        continue;
      }
      if (srvid != -1 && srv.srvid != srvid) {
        continue;
      }
      if (grpfilter != mib::badoff && srv.group_idx != grpfilter) {
        continue;
      }
      if (!srvname.empty() && srvname != srv.servername) {
        continue;
      }
      start(srv, no, d1, any_failed);
    }
    // 22.x tmboot exits non-zero when any server initialization failed;
    // 12.x exits 0 either way
    if (fux::compat::is221 && any_failed) {
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
