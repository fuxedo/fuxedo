// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <atmi.h>
#include <unistd.h>

#include "extreader.h"
#include "fux.h"

using fux::fml32buf;

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool noreply = false;
  bool noprint = false;
  bool nflag = false;  // accepted for compatibility; no effect
  long timeout = -1;
  long delay = 0;
  long sleeptime = 0;
  long error_limit = -1;
  long blocktime = 0;
  std::string usrname;
  std::string cltname;
  std::string codeset;
  std::string bufsz;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(usrname, "usrname")["-U"](
          "Uses usrname as the username when joining the application.") |
      clara::Opt(usrname, "cltname")["-C"](
          "Uses cltname as the client name when joining the application.") |
      clara::Opt(timeout, "timeout")["-t"](
          "timeout is the time, in seconds, before the transaction is timed "
          "out.") |
      clara::Opt(delay, "delay")["-d"](
          "delay in seconds between service calls") |
      clara::Opt(sleeptime, "sleeptime")["-s"](
          "time in seconds to wait before the first call") |
      clara::Opt(error_limit, "error_limit")["-e"](
          "stop after this many failed calls") |
      clara::Opt(blocktime, "blocktime")["-b"](
          "blocking timeout in seconds for the service calls") |
      clara::Opt(codeset, "codeset")["-u"](
          "codeset name (accepted for compatibility; UTF-8 only)") |
      clara::Opt(bufsz, "buffersize")["-S"](
          "buffer size hint (accepted for compatibility)") |
      clara::Opt(nflag)["-n"]("accepted for compatibility; no effect") |
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
  if (show_help) {
    std::cout << parser;
    return 0;
  }

  FLDID32 SRVCNM = Fldid32(const_cast<char *>("SRVCNM"));
  if (SRVCNM == BADFLDID) {
  }
  if (blocktime > 0) {
    tpsblktime(static_cast<int>(blocktime), TPBLK_ALL);
  }
  if (sleeptime > 0) {
    sleep(static_cast<unsigned int>(sleeptime));
  }
  try {
    long errors = 0;
    // each buffer on stdin is one call; a blank line separates buffers
    while (true) {
      extreader r(stdin);
      if (!r.parse()) {
        break;
      }
      FBFR32 *fbfr = r.get();
      fux::fml32buf rq(&fbfr);

      char *srvcnm = Ffind32(fbfr, SRVCNM, 0, nullptr);
      if (srvcnm == nullptr) {
        fprintf(stderr, "No service name found (%s)\n",
                Fstrerror32(Ferror32));
        errors++;
        if (error_limit > 0 && errors >= error_limit) {
          break;
        }
        continue;
      }
      if (!noprint) {
        Fprint32(fbfr);
      }

      if (timeout > 0) {
        tpbegin(timeout, 0);
      }

      int cd;
      if (noreply) {
        cd = tpacall(srvcnm, reinterpret_cast<char *>(rq.ptr()), 0, TPNOFLAGS);
      } else {
        long olen = 0;
        cd = tpcall(srvcnm, reinterpret_cast<char *>(rq.ptr()), 0,
                    reinterpret_cast<char **>(rq.ptrptr()), &olen, TPNOFLAGS);
      }
      if (cd == -1) {
        fprintf(stderr, "%s failed %d %s\n", srvcnm, tperrno,
                tpstrerror(tperrno));
        errors++;
      }

      if (!noprint) {
        Fprint32(fbfr);
      }
      if (cd == -1) {
        if (timeout > 0) {
          tpabort(0);
        }
      } else if (timeout > 0) {
        tpcommit(0);
      }
      if (error_limit > 0 && errors >= error_limit) {
        break;
      }
      if (delay > 0) {
        sleep(static_cast<unsigned int>(delay));
      }
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
