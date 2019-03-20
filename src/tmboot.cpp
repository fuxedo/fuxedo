// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>

#include "mib.h"
#include "misc.h"

bool alive(pid_t pid) { return kill(pid, 0) == 0; }

static void start(server &srv) {
  std::cout << "exec " << srv.servername << " " << srv.clopt << " :"
            << std::endl;
  if (srv.pid != 0 && alive(srv.pid)) {
    std::cout << "\tINFO: Duplicate server." << std::endl;
    return;
  }

  auto pid = fork();
  if (pid == 0) {
    std::vector<const char *> argv;
    argv.push_back(srv.servername);
    argv.push_back("-g");
    auto grpno = std::to_string(srv.grpno);
    argv.push_back(grpno.c_str());
    argv.push_back("-i");
    auto srvid = std::to_string(srv.srvid);
    argv.push_back(srvid.c_str());
    argv.push_back("-A");
    argv.push_back(nullptr);

    if (execvp(srv.servername, (char *const *)&argv[0]) == -1) {
      // perror("AAAAAAAA");
      exit(-1);
    }
  } else if (pid > 0) {
    srv.pid = pid;
    if (alive(pid)) {
      std::cout << "\tprocess id=" << srv.pid << " ... Started." << std::endl;
    } else {
      std::cout << "\tINFO: process id=" << srv.pid << " Assume started (pipe)."
                << std::endl;
    }
  }
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool yes = false;

  auto parser = clara::Help(show_help) |
                clara::Opt(yes)["-y"]("answer Yes to all questions");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }

  try {
    mib &m = getmib();

    std::ostringstream path;
    path << m.mach().tuxdir << "/bin:";
    path << m.mach().appdir << ":";
    path << std::getenv("PATH");

    setenv("PATH", path.str().c_str(), 1);

    auto servers = m.servers();
    for (size_t i = 0; i < servers->len; i++) {
      auto &srv = servers[i];
      if (srv.autostart) {
        start(srv);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
