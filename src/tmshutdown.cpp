// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <tpadm.h>
#include <userlog.h>

#include "fux.h"
#include "ipc.h"

#include "mib.h"

int tpacall_queue(int msqid, const char *svc, char *data, long len, long flags);

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool yes = false;

  int srvid = -1;
  std::string grpname;
  std::string srvname;
  std::string lmid;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(yes)["-y"]("answer Yes to all questions") |
      clara::Opt(srvid, "srvid")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpname, "grpname")["-g"]("server's SRVGRP in TUXCONFIG") |
      clara::Opt(srvname, "aout")["-s"]("shut down only servers named aout") |
      clara::Opt(lmid, "lmid")["-l"]("shut down only on the named LMID");

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

    if (!lmid.empty() && lmid != m.mach().lmid) {
      std::cerr << "lmid " << lmid << " not found on this machine"
                << std::endl;
      return -1;
    }

    fux::fml32buf buf;

    auto queues = m.queues();
    auto servers = m.servers();

    size_t grpfilter = mib::badoff;
    if (!grpname.empty()) {
      grpfilter = m.group_index(grpname);
      if (grpfilter == mib::badoff) {
        std::cerr << "group " << grpname << " not found" << std::endl;
        return -1;
      }
    }

    for (int i = servers->len - 1; i >= 0; i--) {
      auto &server = servers[i];
      if (srvid != -1 && srvid != server.srvid) {
        continue;
      }
      if (grpfilter != mib::badoff && server.group_idx != grpfilter) {
        continue;
      }
      if (!srvname.empty() && srvname != server.servername) {
        continue;
      }
      if (server.pid == 0) {
        continue;
      }
      if (server.is_inactive()) {
        continue;
      }
      server.suspend();
      auto &queue = queues.at(server.rqaddr);

      buf.put(TA_SRVID, 0, server.srvid);
      buf.put(TA_GRPNO, 0, server.grpno);

      userlog("Requesting %s -g %d -i %d to shutdown", server.servername,
              server.grpno, server.srvid);

      std::cout << "\tServer Id = " << server.srvid
                << " Group Id = " << server.grpno;

      int cd =
          tpacall_queue(queue.msqid, ".stop",
                        reinterpret_cast<char *>(*buf.ptrptr()), 0, TPNOTRAN);

      if (cd == -1) {
        std::cout << ":  shutdown failed (" << tpstrerror(tperrno) << ")"
                  << std::endl;
      } else {
        if (tpgetrply(&cd, reinterpret_cast<char **>(buf.ptrptr()), 0,
                      TPNOFLAGS) == -1) {
        }

        if (server.is_inactive()) {
          std::cout << ":  shutdown succeeded" << std::endl;
        } else {
          std::cout << ":  shutdown failed (?)" << std::endl;
        }
      }
    }

    tpterm();
    if (srvid == -1 && grpname.empty() && srvname.empty()) {
      m.remove();
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
