// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>
#include <algorithm>

#include "fux.h"
#include "mib.h"

#include <atmi.h>
#include <stdio.h>
#include <tpadm.h>
#include <userlog.h>
#include <xa.h>

#if defined(__cplusplus)
extern "C" {
#endif
void TMIB(TPSVCINFO *);
int _tmrunserver(int);
extern struct xa_switch_t tmnull_switch;
extern int _tmbuilt_with_thread_option;
#if defined(__cplusplus)
}
#endif

static struct tmdsptchtbl_t _tmdsptchtbl[] = {
    {".TMIB", "TMIB", TMIB, 0, 0, NULL}, {NULL, NULL, NULL, 0, 0, NULL}};
static struct tmsvrargs_t tmsvrargs = {
    &tmnull_switch, _tmdsptchtbl, 0,    tpsvrinit, tpsvrdone, _tmrunserver,
    NULL,           NULL,         NULL, NULL,      tprminit,  tpsvrthrinit,
    tpsvrthrdone,   NULL};

struct tmsvrargs_t *_tmgetsvrargs() {
  return &tmsvrargs;
}

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}

static void handle_blocktime() {
  mib &m = getmib();
  auto now = std::chrono::steady_clock::now();

  auto accessers = m.accessers();
  for (size_t i = 0; i < accessers->len; i++) {
    auto &acc = accessers[i];
    if (!acc.valid()) {
      continue;
    }
    if (acc.rpid_timeout < now) {
      fux::ipc::msg req;
      req->cat = fux::ipc::unblock;
      userlog("Sending unblock message to client=%d queue=%0x", int(i),
              acc.rpid);
      fux::ipc::qsend(acc.rpid, req, 0, fux::ipc::flags::notime);
    }
  }
}

static void monitor_servers() {
  mib &m = getmib();
  auto servers = m.servers();

  for (int i = servers->len - 1; i >= 0; i--) {
    auto &server = servers[i];
    if (server.pid == 0) {
      continue;
    }
    if (!alive(server.pid)) {
      server.pid = 0;
    }
  }
}

static void monitor_clients() {
  mib &m = getmib();
  auto accessers = m.accessers();

  for (int i = accessers->len - 1; i >= 0; i--) {
    auto &accesser = accessers[i];
    if (accesser.pid == 0) {
      continue;
    }
    if (!alive(accesser.pid)) {
      accesser.pid = 0;
      if (accesser.rpid != 0) {
        fux::ipc::qdelete(accesser.rpid);
        accesser.rpid = 0;
      }
    }
  }
}

static void run_watchdog() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    handle_blocktime();
    monitor_servers();
    monitor_clients();
  }
}

int tpsvrinit(int argc, char *argv[]) {
  std::thread t(run_watchdog);
  t.detach();
  return 0;
}

extern "C" void TMIB(TPSVCINFO *svcinfo) {
  fux::fml32buf rq(svcinfo);
  fux::fml32buf rs;

  userlog("Processing %s", svcinfo->name);
  tpadmcall(rq.ptr(), rs.ptrptr(), 0);

  fux::tpreturn(TPSUCCESS, 0, rs);
}
