// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
// This implements the TX (Transaction Demarcation) specification by calling XA
// APIs
// + some TX-like API needed for Oracle Tuxedo APIs

/* XID format:
   - gtrid 64 bit number based on Twitter Snowflake
   - bqual 32 bits
      - grpno 16 bit group identifier from UBBCONFIG
      - conter 16 bit counter: 1 for tightly-coupled and 1...N for loosely
   coupled

 */
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
size_t grpidx = 0;
}  // namespace fux::tx

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
    if (fux::tx::grpno != 0) {
      grpcfg = &mibcon_.group_by_id(fux::tx::grpno);
      fux::tx::grpidx = mibcon_.find_group(fux::tx::grpno);
    }
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

int tx_begin() {
  if (TMREGISTER) {
    // return TX_OK;
  }
  auto gtrid = getmib().gengtrid();
  auto &transactions = getmib().transactions();
  auto n = transactions.start(gtrid);

  TXINFO info;
  info.xid = fux::make_xid(gtrid);
  return _tx_resume(&info);
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
  return rc;
}

struct branch {
  fux::bqual bqual;
  uint16_t grpno;
  int cd;
  int result;
};

static fux::bqual make_bqual(uint16_t grpno, uint16_t n) {
  return grpno << 16 | n;
}

static void command(std::vector<branch> &branches, XID *xid, int command);

static void persist_tlog(XID *xid, std::vector<branch> branches) {
  // TODO
}
static void clean_tlog(XID *xid) {
  // TODO
}

static std::vector<branch> collect(XID *xid) {
  std::vector<branch> branches;
  auto max = getmib().transactions().max_groups;
  auto &tx = getmib().transactions().at(fux::to_gtrid(xid));
  for (size_t group = 0; group < max; group++) {
    int n = tx.groups[group];
    for (int branch = n; branch > 0; branch--) {
      auto grpno = getmib().groups().at(group).grpno;
      branches.push_back({make_bqual(grpno, branch), grpno});
    }
  }
  return branches;
}

int tx_commit() {
  TXINFO info;
  auto rc = _tx_suspend(&info);
  if (rc != TX_OK) {
    return rc;
  }

  std::vector<branch> branches1 = collect(&info.xid);

  command(branches1, &info.xid, fux::tm::prepare);

  // Collect interested branches for the 2nd phase
  std::vector<branch> branches2;
  bool commit = true;
  for (auto &p : branches1) {
    if (p.result == XA_RDONLY) {
      // Nothing to do
      continue;
    }
    if (p.result != XA_OK) {
      commit = false;
    }
    branches2.push_back(p);
  }

  if (commit) {
    persist_tlog(&info.xid, branches2);
  }

  if (commit) {
    command(branches2, &info.xid, fux::tm::commit);
    clean_tlog(&info.xid);
    return change_state(TX_OK);
  } else {
    command(branches2, &info.xid, fux::tm::rollback);
    return change_state(TX_FAIL);
  }
}

int tx_rollback() {
  TXINFO info;
  auto rc = _tx_suspend(&info);
  if (rc != TX_OK) {
    return rc;
  }

  std::vector<branch> branches = collect(&info.xid);

  command(branches, &info.xid, fux::tm::rollback);

  return change_state(TX_OK);
}

int _tx_suspend(TXINFO *info) {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  *info = getctxt().info;
  userlog("xa_end(%s, ...) ...", fux::to_string(&info->xid).c_str());
  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMSUCCESS);
  //                                 TMSUSPEND);
  userlog("xa_end(%s, ...) = %d", fux::to_string(&info->xid).c_str(), xarc);
  getctxt().notrx();

  int rc = TX_OK;
  if (xarc == XA_OK) {
    // pass
  } else if (xarc == XAER_RMFAIL) {
    rc = TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    rc = TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    rc = TX_FAIL;
  } else {
    rc = TX_FAIL;
  }

  return change_state(rc);
}

static void fill_bqual(bool *isnew) {
  auto &xid = getctxt().info.xid;
  if (xid.bqual_length != 0) {
    return;
  }
  auto &n = getmib().transactions().bqual(fux::to_gtrid(&xid), fux::tx::grpidx);
  //  if (n == 0) {
  *isnew = true;
  //  }
  n++;
  fux::bqual bqual = make_bqual(fux::tx::grpno, n);

  std::copy_n(reinterpret_cast<char *>(&bqual), sizeof(bqual),
              xid.data + xid.gtrid_length);
  xid.bqual_length = sizeof(bqual);
}

int _tx_resume(TXINFO *info) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2)) {
    return TX_PROTOCOL_ERROR;
  }

  getctxt().info.xid = info->xid;
  bool isnew = false;
  fill_bqual(&isnew);

  long flags = TMNOFLAGS;
  if (!isnew) {
    flags = TMJOIN;
  }
  userlog("xa_start(%s, %d, 0x%0lx) ...",
          fux::to_string(&getctxt().info.xid).c_str(), fux::tx::grpno, flags);
  auto xarc = xasw->xa_start_entry(&getctxt().info.xid, fux::tx::grpno, flags);
  //                                   TMRESUME);
  userlog("xa_start(%s, %d, 0x%0lx) = %d",
          fux::to_string(&getctxt().info.xid).c_str(), fux::tx::grpno, flags,
          xarc);
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

//////////////////////////////////////////////////////////////////////////////
// Call TM

int tpacall_internal(int grpno, char *svc, char *data, long len, long flags);
void command(std::vector<branch> &branches, XID *xid, int command) {
  fux::fml32buf buf;
  int pending = 0;

  for (auto &p : branches) {
    buf.put(fux::tm::FUX_XAFUNC, 0, command);
    XID bxid = fux::make_xid(fux::to_gtrid(xid), p.bqual);
    buf.put(fux::tm::FUX_XAXID, 0, fux::to_string(&bxid));
    buf.put(fux::tm::FUX_XARMID, 0, p.grpno);
    buf.put(fux::tm::FUX_XAFLAGS, 0, 0);
    userlog("Calling TM in group %d for %s", p.grpno,
            fux::to_string(&bxid).c_str());
    p.cd =
        tpacall_internal(p.grpno, const_cast<char *>(".TM"),
                         reinterpret_cast<char *>(*buf.ptrptr()), 0, TPNOTIME);
    if (p.cd == -1) {
      userlog("Failed to call TM in group %d (%s)", p.grpno,
              tpstrerror(tperrno));
      p.result = TMER_TMERR;
      break;
    }
    pending++;
  }

  while (pending > 0) {
    int cd;
    long len = 0;
    int rc =
        tpgetrply(&cd, reinterpret_cast<char **>(buf.ptrptr()), &len, TPGETANY);

    for (auto &p : branches) {
      if (p.cd == cd) {
        p.result = buf.get<long>(fux::tm::FUX_XARET, 0);
        pending--;
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////
// TM implementation

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
