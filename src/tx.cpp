// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
// This implements the TX (Transaction Demarcation) specification by calling XA
// APIs
// + some TX-like API needed for Oracle Tuxedo APIs

#include <atmi.h>
#include <tx.h>
#include <userlog.h>
#include <xa.h>
#include <cstdint>
#include <memory>

#include <algorithm>

#include "fields.h"
#include "fux.h"
#include "mib.h"
#include "trx.h"

extern "C" {
extern struct xa_switch_t tmnull_switch;
}

namespace fux::tx {
uint16_t grpno = 0;
}

namespace fux::glob {
xa_switch_t *xasw = &tmnull_switch;
}

using fux::glob::xasw;

enum class tx_state {
  s0 = 0,  // S0 No RMs have been opened or initialised. An application thread
           // of control cannot start a global transaction until it has
           // successfully opened its RMs via tx_open().
  s1,      // S1 The thread has opened its RMs but is not in a transaction. Its
           // transaction_control characteristic is TX_UNCHAINED.
  s2,      // S2 The thread has opened its RMs but is not in a transaction. Its
           // transaction_control characteristic is TX_CHAINED.
  s3,      // S3 The thread has opened its RMs and is in a transaction. Its
           // transaction_control characteristic is TX_UNCHAINED.
  s4       // S4 The thread has opened its RMs and is in a transaction. Its
           // transaction_control characteristic is TX_CHAINED.
};

struct tx_context {
  tx_context(mib &mibcon) : mibcon_(mibcon) {
    state = tx_state::s0;
    grpcfg = &mibcon_.group_by_id(fux::tx::grpno);
    notrx();
  }
  void notrx() { info.xid.formatID = -1; }
  tx_state state;
  group *grpcfg = nullptr;
  TXINFO info;
  mib &mibcon_;
};

static thread_local std::shared_ptr<tx_context> txctxt;
static tx_context &getctxt() {
  if (!txctxt.get()) {
    txctxt = std::make_shared<tx_context>(getmib());
  }
  return *txctxt;
}

static int rm_err(int xarc, const char *func) {
  if (xarc == XAER_RMERR) {
    return TX_ERROR;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  }
  return TX_FAIL;
}

int tx_open() {
  auto xarc = xasw->xa_open_entry(getctxt().grpcfg->openinfo,
                                  getctxt().grpcfg->grpno, TMNOFLAGS);
  if (xarc == XA_OK) {
    if (getctxt().state == tx_state::s0) {
      getctxt().notrx();
      getctxt().state = tx_state::s1;
    }
    return TX_OK;
  }

  return rm_err(xarc, "xa_open");
}

int tx_close() {
  if (!(getctxt().state == tx_state::s0 || getctxt().state == tx_state::s1 ||
        getctxt().state == tx_state::s2)) {
    return TX_PROTOCOL_ERROR;
  }
  auto xarc = xasw->xa_close_entry(getctxt().grpcfg->closeinfo,
                                   getctxt().grpcfg->grpno, TMNOFLAGS);
  if (xarc == XA_OK) {
    getctxt().state = tx_state::s0;
    return TX_OK;
  }

  return rm_err(xarc, "xa_close");
}

int tx_info(TXINFO *info) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2 ||
        getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  if (info != nullptr) {
    *info = getctxt().info;
  }

  return getctxt().info.xid.formatID == -1 ? 0 : 1;
}

static void genxid(XID *xid) {
  xid->formatID = fux::FORMAT_ID;
  xid->gtrid_length = sizeof(fux::trxid);
  xid->bqual_length = 0;
  auto trxid = getmib().gentrxid();
  std::copy_n(reinterpret_cast<char *>(&trxid), sizeof(fux::trxid), xid->data);
}

int tx_begin() {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2)) {
    return TX_PROTOCOL_ERROR;
  }

  if (TMREGISTER) {
    // return TX_OK;
  }

  genxid(&getctxt().info.xid);
  // TMJOIN, TMRESUME
  auto xarc = xasw->xa_start_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                   TMNOFLAGS);
  if (xarc == XA_OK) {
    if (getctxt().state == tx_state::s1) {
      getctxt().state = tx_state::s3;
    } else if (getctxt().state == tx_state::s2) {
      getctxt().state = tx_state::s4;
    }
    return TX_OK;
  }

  if (xarc == XAER_RMERR) {
    return TX_ERROR;
  } else if (xarc == XAER_DUPID) {
    return TX_ERROR;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_ERROR;
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_OUTSIDE) {
    return TX_OUTSIDE;
  }

  return TX_FAIL;
}

static bool tx_set1(int rc) {
  return rc == TX_OK || rc == TX_ROLLBACK || rc == TX_MIXED ||
         rc == TX_HAZARD || rc == TX_COMMITTED;
}

static bool tx_set2(int rc) {
  return rc == TX_NO_BEGIN || rc == TX_ROLLBACK_NO_BEGIN ||
         rc == TX_MIXED_NO_BEGIN || rc == TX_HAZARD_NO_BEGIN ||
         rc == TX_COMMITTED_NO_BEGIN;
}

static int change_state(int rc) {
  if (tx_set1(rc)) {
    if (getctxt().state == tx_state::s3) {
      getctxt().state = tx_state::s1;
    } else if (getctxt().state == tx_state::s4) {
      // Chained
      getctxt().state = tx_state::s2;
      return tx_begin();
    }
  } else if (tx_set2(rc)) {
    if (getctxt().state == tx_state::s4) {
      getctxt().state = tx_state::s2;
    }
  }
  getctxt().notrx();
  return rc;
}

static int xa_end_err(int xarc) {
  if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  } else {
    return TX_FAIL;
  }
}

struct participant {
  uint16_t grpno;
  size_t mibptr;
  int cd;
  int result;
};

int tpacall_internal(int grpno, char *svc, char *data, long len, long flags);

static void command(std::vector<participant> &participants, XID *xid,
                    int command) {
  fux::fml32buf buf;
  int pending = 0;

  for (auto &p : participants) {
    buf.put(fux::tm::FUX_XAFUNC, 0, command);
    buf.put(fux::tm::FUX_XAXID, 0, fux::to_string(xid));
    buf.put(fux::tm::FUX_XARMID, 0, p.grpno);
    buf.put(fux::tm::FUX_XAFLAGS, 0, 0);
    p.cd =
        tpacall_internal(p.grpno, const_cast<char *>(".TM"),
                         reinterpret_cast<char *>(*buf.ptrptr()), 0, TPNOTIME);
    pending++;
  }

  while (pending > 0) {
    int cd;
    long len = 0;
    int rc =
        tpgetrply(&cd, reinterpret_cast<char **>(buf.ptrptr()), &len, TPGETANY);

    for (auto &p : participants) {
      if (p.cd == cd) {
        p.result = buf.get<long>(fux::tm::FUX_XARET, 0);
      }
    }
  }
}

static void persist_tlog(XID *xid, std::vector<participant> participants) {
  // TODO
}
static void clean_tlog(XID *xid) {
  // TODO
}

int tx_commit() {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMNOFLAGS);
  if (xarc == XA_OK) {
    // pass
  } else {
    return change_state(xa_end_err(xarc));
  }

  std::vector<participant> participants1;
  command(participants1, &getctxt().info.xid, fux::tm::prepare);

  // Collect interested participants for the 2nd phase
  std::vector<participant> participants2;
  bool commit = true;
  for (auto &p : participants1) {
    if (p.result == XA_RDONLY) {
      // Nothing to do
      continue;
    }

    if (p.result != XA_OK) {
      commit = false;
    }
    participants2.push_back(p);
  }

  if (commit) {
    persist_tlog(&getctxt().info.xid, participants2);
  }

  if (commit) {
    command(participants2, &getctxt().info.xid, fux::tm::commit);
    clean_tlog(&getctxt().info.xid);
    return change_state(TX_OK);
  } else {
    command(participants2, &getctxt().info.xid, fux::tm::rollback);
    return change_state(TX_FAIL);
  }
}

int tx_rollback() {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMNOFLAGS);
  if (xarc == XA_OK) {
    // pass
  } else {
    return change_state(xa_end_err(xarc));
  }

  std::vector<participant> participants;
  command(participants, &getctxt().info.xid, fux::tm::rollback);
  return change_state(TX_OK);
}

int _tx_suspend(TXINFO *info) {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMSUSPEND);
  memcpy(info, &(getctxt().info), sizeof(*info));
  if (xarc == XA_OK) {
    // pass
  } else {
    return xa_end_err(xarc);
  }

  getctxt().notrx();
  if (getctxt().state == tx_state::s3) {
    getctxt().state = tx_state::s1;
  } else if (getctxt().state == tx_state::s4) {
    getctxt().state = tx_state::s2;
  }
  return TX_OK;
}

int _tx_resume(TXINFO *info) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2)) {
    return TX_PROTOCOL_ERROR;
  }
  memcpy(&(getctxt().info.xid), &(info->xid), sizeof(info->xid));

  auto xarc = xasw->xa_start_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                   TMRESUME);
  if (xarc == XA_OK) {
    if (getctxt().state == tx_state::s1) {
      getctxt().state = tx_state::s3;
    } else if (getctxt().state == tx_state::s2) {
      getctxt().state = tx_state::s4;
    }
    return TX_OK;
  }

  if (xarc == XAER_RMERR) {
    return TX_ERROR;
  } else if (xarc == XAER_DUPID) {
    return TX_ERROR;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_ERROR;
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_OUTSIDE) {
    return TX_OUTSIDE;
  }

  return TX_FAIL;
}

int tx_set_commit_return(COMMIT_RETURN when_return) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2 ||
        getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  if (!(when_return == TX_COMMIT_DECISION_LOGGED ||
        when_return == TX_COMMIT_COMPLETED)) {
    return TX_EINVAL;
  }

  getctxt().info.when_return = when_return;
  return TX_OK;
}
int tx_set_transaction_control(TRANSACTION_CONTROL control) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2 ||
        getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }
  if (!(control == TX_UNCHAINED || control == TX_CHAINED)) {
    return TX_EINVAL;
  }
  getctxt().info.transaction_control = control;
  if (getctxt().state == tx_state::s1 && control == TX_CHAINED) {
    getctxt().state = tx_state::s2;
  } else if (getctxt().state == tx_state::s2 && control == TX_UNCHAINED) {
    getctxt().state = tx_state::s1;
  } else if (getctxt().state == tx_state::s3 && control == TX_CHAINED) {
    getctxt().state = tx_state::s4;
  } else if (getctxt().state == tx_state::s4 && control == TX_UNCHAINED) {
    getctxt().state = tx_state::s3;
  }
  return TX_OK;
}
int tx_set_transaction_timeout(TRANSACTION_TIMEOUT timeout) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2 ||
        getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }
  if (timeout < 0) {
    return TX_EINVAL;
  }
  getctxt().info.transaction_timeout = timeout;
  return TX_OK;
}

static void tm(fux::fml32buf &buf) {
  auto func = buf.get<long>(fux::tm::FUX_XAFUNC, 0);
  auto xids = buf.get<std::string>(fux::tm::FUX_XAXID, 0);
  int rmid = buf.get<long>(fux::tm::FUX_XARMID, 0);
  long flags = buf.get<long>(fux::tm::FUX_XAFLAGS, 0);

  XID xid = fux::to_xid(xids);
  int ret;
  switch (func) {
    case fux::tm::prepare:
      userlog("xa_prepare(%s, %d, 0x%0lx) ...", xids.c_str(), rmid, flags);
      ret = xasw->xa_prepare_entry(&xid, rmid, flags);
      userlog("xa_prepare(%s, %d, 0x%0lx) = %d", xids.c_str(), rmid, flags,
              ret);
      break;
    case fux::tm::commit:
      userlog("xa_commit(%s, %d, 0x%0lx) ...", xids.c_str(), rmid, flags);
      ret = xasw->xa_commit_entry(&xid, rmid, flags);
      userlog("xa_commit(%s, %d, 0x%0lx) = %d", xids.c_str(), rmid, flags, ret);
      break;
    case fux::tm::rollback:
      userlog("xa_rollback(%s, %d, 0x%0lx) ...", xids.c_str(), rmid, flags);
      ret = xasw->xa_rollback_entry(&xid, rmid, flags);
      userlog("xa_rollback(%s, %d, 0x%0lx) = %d", xids.c_str(), rmid, flags,
              ret);
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
