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

#include "fux.h"
#include "fields.h"
#include "ipc.h"

#include "mib.h"

int main(int argc, char *argv[]) {
  bool show_help = false;

  int srvid = -1;
  std::string grpname;

  auto parser = clara::Help(show_help) |
      clara::Opt(srvid, "srvid")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpname, "grpname")["-g"]("server's SRVGRP in TUXCONFIG");

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
    mib &m = getmib();

    fux::fml32buf buf;
    fux::ipc::msg req;
    req->cat = fux::ipc::admin;

    auto servers = m.servers();
    auto queues = m.queues();
    for (size_t i = 0; i < servers->len; i++) {
      auto &server = servers[i];
      auto &queue = queues.at(server.rqaddr);
      buf.put(FUX_SRVID, 0, server.srvid);
      buf.put(FUX_GRPNO, 0, server.grpno);
      req.set_data(reinterpret_cast<char *>(*buf.fbfr()), 0);
      req->mtype = queue.mtype--;
      fux::ipc::qsend(queue.msqid, req, 0);
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
