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

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "mib.h"

bool alive(pid_t pid) { return kill(pid, 0) == 0; }

static void start(server &srv) {
  if (srv.pid != 0 && alive(srv.pid)) {
    printf("Already running\n");
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

    if (execv(srv.servername, (char *const *)&argv[0]) == -1) {
      perror("AAAAAAAA");
    }
  } else if (pid > 0) {
    srv.pid = pid;
  }
}

int main(int argc, char *argv[]) {
  bool show_help = false;

  auto parser = clara::Parser() | clara::Help(show_help);

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }

  try {
    mib &m = getmib();

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
