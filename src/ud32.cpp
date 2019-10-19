// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <atmi.h>

#include "extreader.h"
#include "fux.h"

using fux::fml32buf;

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool noreply = false;
  bool noprint = false;
  long timeout = -1;
  std::string usrname;
  std::string cltname;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(usrname, "usrname")["-U"](
          "Uses usrname as the username when joining the application.") |
      clara::Opt(usrname, "cltname")["-C"](
          "Uses cltname as the client name when joining the application.") |
      clara::Opt(timeout, "timeout")["-t"](
          "timeout is the time, in seconds, before the transaction is timed "
          "out.") |
      clara::Opt(noprint)["-p"](
          "Suppresses printing of the fielded buffers that were sent and "
          "returned.") |
      clara::Opt(noreply)["-r"](
          "ud32 should not expect a reply message from servers.");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }

  try {
    extreader r(stdin);
    auto ptr = r.parse();
    FBFR32 *fbfr = ptr.get();
    FBFR32 *rq;

    FLDID32 SRVCNM = Fldid32(const_cast<char *>("SRVCNM"));
    if (SRVCNM == BADFLDID) {
    }
    char *srvcnm = Ffind32(fbfr, SRVCNM, 0, nullptr);
    if (srvcnm == nullptr) {
    }

    if (timeout > 0) {
      tpbegin(timeout, 0);
    }

    int cd;
    if (noreply) {
      cd = tpacall(srvcnm, reinterpret_cast<char *>(rq), 0, TPNOFLAGS);
    } else {
      long olen = 0;
      cd = tpcall(srvcnm, reinterpret_cast<char *>(rq), 0, reinterpret_cast<char **>(&rq), &olen, TPNOFLAGS);
    }

    if (cd == -1) {
      if (timeout > 0) {
        tpabort(0);
      }
    } else if (timeout > 0) {
      tpcommit(0);
    }
  } catch (const std::system_error &e) {
    std::cerr << e.code().message() << std::endl;
    return -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
