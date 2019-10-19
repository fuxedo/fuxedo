// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>
#include <algorithm>

#include "ctxt.h"
#include "fields.h"
#include "fux.h"
#include "mib.h"

static void tm(fux::fml32buf &buf) {
  auto func = buf.get<long>(fux::tm::FUX_XAFUNC, 0);
  auto xids = buf.get<std::string>(fux::tm::FUX_XAXID, 0);
  int rmid = buf.get<long>(fux::tm::FUX_XARMID, 0);
  long flags = buf.get<long>(fux::tm::FUX_XAFLAGS, 0);

  XID xid;
  std::copy_n(&xids[0], xids.size(), reinterpret_cast<char *>(&xid));
  int ret;
  switch (func) {
    case fux::tm::prepare:
      ret = fux::glob::xaswitch()->xa_prepare_entry(&xid, rmid, flags);
      break;
    case fux::tm::commit:
      ret = fux::glob::xaswitch()->xa_commit_entry(&xid, rmid, flags);
      break;
    case fux::tm::rollback:
      ret = fux::glob::xaswitch()->xa_rollback_entry(&xid, rmid, flags);
      break;
    default:
      ret = XAER_INVAL;
  }
  buf.put(fux::tm::FUX_XARET, 0, ret);
}

extern "C" void TMIB(TPSVCINFO *svcinfo) {
  fux::fml32buf buf(svcinfo);
  tm(buf);

  mib &m = getmib();
  auto now = std::chrono::steady_clock::now();
  auto accessers = m.accessers();
  for (size_t i = 0; i < accessers->len; i++) {
    auto &acc = accessers[i];
    if (acc.rpid_timeout < now) {
      fux::ipc::msg req;
      req->cat = fux::ipc::unblock;
      fux::ipc::qsend(acc.rpid, req, 0, fux::ipc::flags::notime);
    }
  }

  fux::tpreturn(TPSUCCESS, 0, buf);
}
#include <atmi.h>
#include <stdio.h>
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
