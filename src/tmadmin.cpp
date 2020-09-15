// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iomanip>
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

static void do_help() {
  std::cout << "help (h) [{command | all}]" << std::endl;
  std::cout << "printqueue (pq) [qaddress]" << std::endl;
  std::cout << "printserver (psr) [-m machine] [-g groupname [-R rmid]] [-i "
               "srvid] [-q qaddress]"
            << std::endl;
}

/*
printserver (psr)
The printserver command provides information about the work being done by the
application's servers.

Prog Name      Queue Name  2ndQueue Name  Grp Name      ID RqDone Load Done
Current Service
---------      ----------  ----------     --------      -- ------ ---------
--------------- BBL            32769                      tuxapp         0 1 50
(  IDLE ) ecb.py         ecb         #S.00001.00010 GROUP1        10      1 50 (
IDLE ) ecb.py         ecb         #S.00001.00011 GROUP1        11      0 0 (
IDLE )


all> psr
Totals for all machines:
a.out Name    Queue Name  Grp Name      ID RqDone Load Done Machine
----------    ----------  --------      -- ------ --------- -------
BBL           30003.00000 SITE2          0     49      2450 SITE2
BBL           30002.00000 SITE1          0     53      2650 SITE1
DBBL          80952       SITE1          0    460     23000 SITE1
TLR           tlr1        BANKB1         1     55      2750 SITE1
*/

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
      clara::Help(show_help) | clara::Opt(version)["-v"]("show version info");

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

    std::string line;
    std::cout << "> " << std::flush;
    while (getline(std::cin, line)) {
      if (line == "help" || line == "h") {
        do_help();
      } else if (line == "printqueue" || line == "pq") {
        do_printqueue();
      }
      std::cout << "> " << std::flush;
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
