// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>

#include "mib.h"
#include "misc.h"


static void start(server &srv, bool no, bool d1) {
  std::vector<const char *> argv;
  argv.push_back(srv.servername);
  argv.push_back("-g");
  auto grpno = std::to_string(srv.grpno);
  argv.push_back(grpno.c_str());
  argv.push_back("-i");
  auto srvid = std::to_string(srv.srvid);
  argv.push_back(srvid.c_str());
  argv.push_back("-A");

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
      std::cout << "\tFailed." << std::endl;
    }
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
    for (size_t i = 0; i < servers->len; i++) {
      auto &srv = servers[i];
      if (!srv.autostart) {
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
      start(srv, no, d1);
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
