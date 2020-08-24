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
  bool d1 = false;
  std::string grpname;
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
      // FIXME: clara can't handle "-d1"
      clara::Opt(d1)["-d"](
          "causes command line options to be printed on the standard output") |
      clara::Opt(srvid, "srvid")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpname, "grpname")["-g"]("server's SRVGRP in TUXCONFIG");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }

  try {
    mib &m = getmib();

    m.validate();

    std::ostringstream path;
    path << m.mach().tuxdir << "/bin:";
    path << m.mach().appdir << ":";
    path << std::getenv("PATH");

    setenv("PATH", path.str().c_str(), 1);

    auto servers = m.servers();
    for (size_t i = 0; i < servers->len; i++) {
      auto &srv = servers[i];
      if (srv.autostart) {
        start(srv, no, d1);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
