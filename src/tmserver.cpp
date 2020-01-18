// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>
#include <algorithm>

#include "fields.h"
#include "fux.h"

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
      //      ret = fux::glob::xaswitch()->xa_prepare_entry(&xid, rmid, flags);
      break;
    case fux::tm::commit:
      //      ret = fux::glob::xaswitch()->xa_commit_entry(&xid, rmid, flags);
      break;
    case fux::tm::rollback:
      //      ret = fux::glob::xaswitch()->xa_rollback_entry(&xid, rmid, flags);
      break;
    default:
      ret = XAER_INVAL;
  }
  buf.put(fux::tm::FUX_XARET, 0, ret);
}

extern "C" void TM(TPSVCINFO *svcinfo) {
  fux::fml32buf buf(svcinfo);
  tm(buf);
  fux::tpreturn(TPSUCCESS, 0, buf);
}
