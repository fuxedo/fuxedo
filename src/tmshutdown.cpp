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
  std::string ksig;
  bool admin_only = false;
  bool tms_only = false;
  bool noop_wait = false;
  bool noop_pause = false;
  long noop_seq = 0;
  std::string errcmd;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(yes)["-y"]("answer Yes to all questions") |
      clara::Opt(admin_only)["-A"](
          "shut down only administrative servers (BBL, GWADM, DMADM)") |
      clara::Opt(tms_only)["-S"]("shut down only the TMS servers") |
      clara::Opt(errcmd, "errcmd")["-e"](
          "command to run when a server does not shut down") |
      clara::Opt(noop_seq, "sequence")["-o"](
          "shutdown sequence number (accepted for compatibility; ignored)") |
      clara::Opt(noop_wait)["-w"](
          "wait for the shutdown to complete (default behavior; accepted)") |
      clara::Opt(noop_pause)["-p"](
          "pause between server shutdowns (accepted for compatibility; "
          "ignored)") |
      clara::Opt(srvid, "srvid")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpname, "grpname")["-g"]("server's SRVGRP in TUXCONFIG") |
      clara::Opt(srvname, "aout")["-s"]("shut down only servers named aout") |
      clara::Opt(lmid, "lmid")["-l"]("shut down only on the named LMID") |
      clara::Opt(ksig, "signal")["-k"](
          "send signal (e.g. TERM, KILL) to server processes");

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
    bool any_failed = false;
    if (!grpname.empty()) {
      grpfilter = m.group_index(grpname);
      if (grpfilter == mib::badoff) {
        std::cerr << "group " << grpname << " not found" << std::endl;
        return -1;
      }
    }

    for (int i = servers->len - 1; i >= 0; i--) {
      auto &server = servers[i];
      bool is_admin = strcmp(server.servername, "BBL") == 0 ||
                      strcmp(server.servername, "GWADM") == 0 ||
                      strcmp(server.servername, "DMADM") == 0;
      bool is_tms = strncmp(server.servername, "TMS", 3) == 0;
      if (admin_only && !is_admin) {
        continue;
      }
      if (tms_only && !is_tms) {
        continue;
      }
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
      if (!alive(server.pid)) {
        // stale entry from a crashed domain: no one would answer .stop
        server.pid = 0;
        continue;
      }
      if (server.is_inactive()) {
        continue;
      }
      server.suspend();

      std::cout << "\tServer Id = " << server.srvid
                << " Group Id = " << server.grpno;

      if (!ksig.empty()) {
        int signo = ksig == "KILL" ? SIGKILL
                    : ksig == "TERM" ? SIGTERM
                                     : 0;
        if (signo == 0) {
          std::cerr << ":  unknown signal " << ksig << std::endl;
          continue;
        }
        if (kill(server.pid, signo) == -1) {
          std::cout << ":  shutdown failed (" << tpstrerror(tperrno) << ")"
                    << std::endl;
          continue;
        }
        for (int i = 0; i < 50 && alive(server.pid); i++) {
          struct timespec req = {0, 100000000};
          nanosleep(&req, nullptr);
        }
        if (alive(server.pid)) {
          std::cout << ":  shutdown failed (?)" << std::endl;
        } else {
          server.pid = 0;
          std::cout << ":  " << ksig << std::endl;
        }
        continue;
      }

      auto &queue = queues.at(server.rqaddr);

      buf.put(TA_SRVID, 0, server.srvid);
      buf.put(TA_GRPNO, 0, server.grpno);

      userlog("Requesting %s -g %d -i %d to shutdown", server.servername,
              server.grpno, server.srvid);

      int cd =
          tpacall_queue(queue.msqid, ".stop",
                        reinterpret_cast<char *>(*buf.ptrptr()), 0, TPNOTRAN);

      if (cd == -1) {
        std::cout << ":  shutdown failed (" << tpstrerror(tperrno) << ")"
                  << std::endl;
        any_failed = true;
      } else {
        if (tpgetrply(&cd, reinterpret_cast<char **>(buf.ptrptr()), 0,
                      TPNOFLAGS) == -1) {
        }

        if (server.is_inactive()) {
          std::cout << ":  shutdown succeeded" << std::endl;
        } else {
          std::cout << ":  shutdown failed (?)" << std::endl;
          any_failed = true;
        }
      }
    }
    if (any_failed && !errcmd.empty()) {
      if (system(errcmd.c_str()) == -1) {
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
