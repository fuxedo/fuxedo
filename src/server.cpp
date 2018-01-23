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

#include <setjmp.h>
#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

#include <atomic>
#include <clara.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "fields.h"
#include "fux.h"
#include "ipc.h"
#include "mib.h"
#include "misc.h"

#if defined(__cplusplus)
extern "C" {
#endif
int _tmrunserver(int);
int _tmbuilt_with_thread_option = 0;
int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs);
#if defined(__cplusplus)
}
#endif

static void dispatch();

int _tmrunserver(int) {
  if (_tmbuilt_with_thread_option) {
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; i++) {
      threads.emplace_back(std::thread(dispatch));
    }

    for (auto &t : threads) {
      t.join();
    }
  } else {
    dispatch();
  }
  return 0;
}

int tprminit(char *, void *) { return 0; }
int tpsvrinit(int, char **) { return 0; }
void tpsvrdone() {}
int tpsvrthrinit(int, char **) { return 0; }
void tpsvrthrdone() {}

void ubb2mib(ubbconfig &u, mib &m);

struct server_context {
  uint16_t srvid;
  uint16_t grpno;

  size_t srv;
  size_t queue_idx;
  int rqaddr;
  int argc;
  char **argv;
  struct tmsvrargs_t *tmsvrargs;

  std::mutex mutex;
  std::map<const char *, void (*)(TPSVCINFO *), cmp_cstr> advertisements;

  mib &m_;
  std::atomic<bool> stop;

  server_context(mib &m) : m_(m), stop(false), req_counter_(0) {
    mtype_ = std::numeric_limits<long>::min();
  }

  int tpadvertise(const char *svcname, void (*func)(TPSVCINFO *)) {
    fux::scoped_fuxlock lock(mutex);

    if (svcname == nullptr || strlen(svcname) == 0) {
      TPERROR(TPEINVAL, "invalid svcname");
      return -1;
    }
    if (func == nullptr) {
      TPERROR(TPEINVAL, "invalid func");
      return -1;
    }
    /*
       [TPEPROTO]
       tpadvertise() was called in an improper context (for example, by a
       client).
       [TPESYSTEM]
       An Oracle Tuxedo system error has occurred. The exact nature of the error
       is written to a log file.
       [TPEOS]
       An operating system error has occurred.o
       */
    auto it = advertisements.find(svcname);

    if (it != advertisements.end()) {
      if (it->second != func) {
        TPERROR(TPEMATCH, "svcname advertised with a different func");
        return -1;
      }
    } else {
      try {
        auto lock = m_.data_lock();
        m_.advertise(svcname, queue_idx);
      } catch (const std::out_of_range &e) {
        TPERROR(TPELIMIT, "%s", e.what());
        return -1;
      }
      advertisements.insert(std::make_pair(strdup(svcname), func));
    }
    return 0;
  }

  int tpunadvertise(const char *svcname) {
    fux::scoped_fuxlock lock(mutex);
    if (svcname == nullptr || strlen(svcname) == 0) {
      TPERROR(TPEINVAL, "invalid svcname");
      return -1;
    }
    auto it = advertisements.find(svcname);

    if (it != advertisements.end()) {
      auto freeme = it->first;
      advertisements.erase(it);
      free(const_cast<char *>(freeme));
    } else {
      TPERROR(TPENOENT, "svcname not advertised");
    }
    return 0;
  }

  bool handle(long mtype, fux::fml32buf &buf) {
    // Do not want to see this message again
    mtype_ = -(mtype - 1);
    req_counter_ = 0;

    if (buf.get<long>(FUX_SRVID, 0) == srvid &&
        buf.get<long>(FUX_GRPNO, 0) == grpno) {
      stop = true;
      return true;
    }
    return false;
  }

  long mtype() {
    req_counter_ = (req_counter_ + 1) % 8;
    if (req_counter_ == 0) {
      // every nth message screw priorities and read first message from queue
      return 0;
    }
    return mtype_;
  }

 private:
  long mtype_;
  int req_counter_;
};

static std::unique_ptr<server_context> ctxt;

struct server_thread_context {
  server_thread_context() : atmibuf(nullptr) {
    ctxt->tmsvrargs->svrthrinit(ctxt->argc, ctxt->argv);
  }

  ~server_thread_context() { ctxt->tmsvrargs->svrthrdone(); }

  void prepare() {
    if (atmibuf == nullptr) {
      atmibuf = tpalloc(const_cast<char *>("STRING"), nullptr, 4 * 1024);
      fux::mem::setowner(atmibuf, &atmibuf);
    }
  }

  fux::ipc::msg req;
  fux::ipc::msg res;
  char *atmibuf;
  jmp_buf tpreturn_env;

  void tpreturn(int rval, long rcode, char *data, long len, long flags) {
    if (req->replyq != -1) {
      res.set_data(data, len);

      if (data != nullptr && data != atmibuf) {
        tpfree(data);
      }

      if (flags != 0) {
        userlog("tpreturn with flags!=0");
        res->rval = TPESVCERR;
      } else {
        res->rval = rval;
      }
      res->rcode = rcode;
      res->flags = flags;
      res->mtype = req->cd;

      fux::ipc::qsend(req->replyq, res, 0);
    }

    longjmp(tpreturn_env, 1);
  }
};

static thread_local std::unique_ptr<server_thread_context> tctxt;

static void dispatch() {
  tctxt = std::make_unique<server_thread_context>();

  while (true) {
    if (ctxt->stop) {
      break;
    }

    TPSVCINFO tpsvcinfo;
    do {
      fux::scoped_fuxlock lock(ctxt->mutex);
      if (ctxt->stop) {
        break;
      }

      fux::ipc::qrecv(ctxt->rqaddr, tctxt->req, ctxt->mtype(), 0);

      tctxt->prepare();
      tctxt->req.get_data(&tctxt->atmibuf);
      tpsvcinfo.data = tctxt->atmibuf;
      if (tctxt->req->cat == fux::ipc::admin) {
        fux::fml32buf buf(&tpsvcinfo);

        userlog("Received admin message");
        if (!ctxt->handle(tctxt->req->mtype, buf)) {
          // return for processing by other MSSQ servers
          userlog("Not the target receiver of message, put back in queue");
          fux::ipc::qsend(ctxt->rqaddr, tctxt->req, 0);
        }
        continue;
      } else {
        // else continue processing without global lock
      }
    } while (false);

    checked_copy(tctxt->req->servicename, tpsvcinfo.name);
    tpsvcinfo.flags = tctxt->req->flags;
    tpsvcinfo.len = tctxt->req.size_data();
    tpsvcinfo.cd = tctxt->req->cd;

    auto it = ctxt->advertisements.find(tpsvcinfo.name);
    if (setjmp(tctxt->tpreturn_env) == 0) {
      it->second(&tpsvcinfo);
    } else {
    }
  }

  tctxt.reset();
}

int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs) {
  bool show_help = false;
  bool verbose = false;

  bool all = false;
  int grpno = -1;
  int srvid = -1;
  std::vector<std::string> services;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(srvid, "SRVID")["-i"]("server's SRVID in TUXCONFIG") |
      clara::Opt(grpno, "GRPNO")["-g"]("server's GRPNO in TUXCONFIG") |
      clara::Opt(services, "SERVICES")["-s"]("services to advertise") |
      clara::Opt(all)["-A"]("advertise all services") |
      clara::Opt(verbose)["-v"]("display built-in services");

  int sep = 0;
  for (; sep < argc; sep++) {
    if (strcmp(argv[sep], "--") == 0) {
      break;
    }
  }

  auto result = parser.parse(clara::Args(sep, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  if (show_help) {
    std::cout << parser;
    return 0;
  }

  if (verbose) {
    struct tmdsptchtbl_t *rec = tmsvrargs->tmdsptchtbl;
    std::cout << "# Service : Function mapping" << std::endl;
    while (rec->svcname != nullptr) {
      std::cout << rec->svcname << ":" << rec->funcname << std::endl;
      rec++;
    }
    return 0;
  }

  if (_tmbuilt_with_thread_option) {
    fux::threaded = true;
  }

  mib &m = getmib();
  ctxt = std::make_unique<server_context>(m);

  ctxt->srv = m.find_server(srvid, grpno);
  if (ctxt->srv == m.badoff) {
    return -1;
  }

  ctxt->srvid = srvid;
  ctxt->grpno = grpno;

  ctxt->rqaddr = m.make_service_rqaddr(ctxt->srv);
  ctxt->queue_idx = m.servers().at(ctxt->srv).rqaddr;
  ctxt->argc = argc;
  ctxt->argv = argv;
  ctxt->tmsvrargs = tmsvrargs;

  if (all) {
    struct tmdsptchtbl_t *rec = tmsvrargs->tmdsptchtbl;
    while (rec->svcname != nullptr && strlen(rec->svcname) > 0) {
      if (tpadvertise(rec->svcname, rec->svcfunc) == -1) {
        userlog("Failed to advertise service %s / function %s : %s",
                rec->svcname, rec->funcname, tpstrerror(tperrno));
        return -1;
      }
      rec++;
    }
  }

  tmsvrargs->svrinit(argc, argv);
  tmsvrargs->mainloop(0);
  tmsvrargs->svrdone();
  return 0;
}

int tpadvertise(FUXCONST char *svcname, void (*func)(TPSVCINFO *)) {
  return ctxt->tpadvertise(svcname, func);
}

int tpunadvertise(FUXCONST char *svcname) {
  return ctxt->tpunadvertise(svcname);
}

void tpreturn(int rval, long rcode, char *data, long len, long flags) try {
  return tctxt->tpreturn(rval, rcode, data, len, flags);
} catch (...) {
  longjmp(tctxt->tpreturn_env, -1);
}
