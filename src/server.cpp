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

#include <clara.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

#include "ipc.h"
#include "mib.h"
#include "misc.h"

#if defined(__cplusplus)
extern "C" {
#endif

int _tmrunserver(int);
// struct xa_switch_t tmnull_switch;
int _tmbuilt_with_thread_option = 0;
int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs);
xa_switch_t tmnull_switch;
#if defined(__cplusplus)
}
#endif

static void run_dispatcher();

int _tmrunserver(int) { run_dispatcher(); }

int tprminit(char *, void *) { return 0; }
int tpsvrinit(int, char **) { return 0; }
void tpsvrdone() {}
int tpsvrthrinit(int, char **) { return 0; }
void tpsvrthrdone() {}

void ubb2mib(ubbconfig &u, mib &m);

struct server_context {
  size_t srv;
  size_t queue;
  int rqaddr;
};

static server_context ctxt;

struct server_thread_context {
  fux::ipc::msg req;
  fux::ipc::msg res;
  char *atmibuf;
  jmp_buf tpreturn_env;
};

static server_thread_context tctxt;

static std::map<const char *, void (*)(TPSVCINFO *), cmp_cstr> advertisements;

static void run_dispatcher() {
  tctxt.atmibuf = tpalloc(const_cast<char *>("STRING"), nullptr, 1024);
  fux::mem::setowner(tctxt.atmibuf, &tctxt.atmibuf);
  while (true) {
    std::cout << "Listen: " << ctxt.rqaddr << std::endl;
    fux::ipc::qrecv(ctxt.rqaddr, tctxt.req, 0, 0);
    std::cout << "Requested service: " << tctxt.req.as_memmsg().servicename
              << std::endl;
    std::cout << "Data len: " << tctxt.req.size_data() << std::endl;
    std::cout << "Data: " << tctxt.req.as_memmsg().data << std::endl;

    tctxt.req.get_data(&tctxt.atmibuf);
    auto &msg = tctxt.req.as_memmsg();
    std::cout << "Data: " << tctxt.atmibuf << std::endl;

    TPSVCINFO tpsvcinfo;
    checked_copy(msg.servicename, tpsvcinfo.name);
    tpsvcinfo.flags = msg.flags;
    tpsvcinfo.data = tctxt.atmibuf;
    tpsvcinfo.len = tctxt.req.size_data();
    tpsvcinfo.cd = msg.cd;

    auto it = advertisements.find(tpsvcinfo.name);
    if (setjmp(tctxt.tpreturn_env) == 0) {
      it->second(&tpsvcinfo);
    } else {
    }
  }
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
      clara::Opt(all)["-a"]("advertise all services") |
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

  mib &m = getmib();

  ctxt.srv = m.find_server(srvid, grpno);
  if (ctxt.srv == m.badoff) {
    return -1;
  }

  ctxt.rqaddr = m.make_service_rqaddr(ctxt.srv);
  ctxt.queue = m.servers().at(ctxt.srv).rqaddr;

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
  tmsvrargs->svrthrinit(argc, argv);

  tmsvrargs->mainloop(0);

  tmsvrargs->svrthrdone();
  tmsvrargs->svrdone();
  tmsvrargs->svrdone();
  return 0;
}

int tpadvertise(FUXCONST char *svcname, void (*func)(TPSVCINFO *)) {
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
     tpadvertise() was called in an improper context (for example, by a client).
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
      auto m = getmib();
      auto lock = m.data_lock();
      m.advertise(svcname, ctxt.queue);
    } catch (const std::out_of_range &e) {
      TPERROR(TPELIMIT, "%s", e.what());
      return -1;
    }
    advertisements.insert(std::make_pair(strdup(svcname), func));
  }
  return 0;
}

int tpunadvertise(FUXCONST char *svcname) {
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

void tpreturn(int rval, long rcode, char *data, long len, long flags) try {
  tctxt.res.set_data(data, len);

  auto &msg = tctxt.res.as_memmsg();
  if (flags != 0) {
    userlog("tpreturn with flags!=0");
    msg.rval = TPESVCERR;
  } else {
    msg.rval = rval;
  }
  msg.rcode = rcode;
  msg.flags = flags;
  msg.mtype = tctxt.req.as_memmsg().cd;

  fux::ipc::qsend(tctxt.req.as_memmsg().replyq, tctxt.res, 0);

  longjmp(tctxt.tpreturn_env, 1);
} catch (...) {
  longjmp(tctxt.tpreturn_env, -1);
}
