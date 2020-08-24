// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
// This implements the TX (Transaction Demarcation) specification by calling XA
// APIs
// + some TX-like API needed for Oracle Tuxedo APIs

#include <atmi.h>
#include <tpadm.h>
#include <tx.h>
#include <userlog.h>
#include <xa.h>
#include <cstdint>
#include <memory>

#include <algorithm>

#include "fux.h"
#include "mib.h"
#include "trx.h"

extern "C" {
extern struct xa_switch_t tmnull_switch;
}

namespace fux::tm {
enum xa_func { prepare, commit, rollback };
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
  void notrx() {
    info.xid.formatID = -1;
    gttid = fux::bad_gttid;
  }
  tx_state state;
  group *grpcfg = nullptr;
  TXINFO info;
  fux::gttid gttid;
  mib &mibcon_;
};

static thread_local std::shared_ptr<tx_context> txctxt;
static tx_context &getctxt() {
  if (!txctxt.get()) {
    txctxt = std::make_shared<tx_context>(getmib());
  }
  return *txctxt;
}

int _tx_start(TXINFO *info);
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
  char *info = getctxt().grpcfg->openinfo;
  auto len = strlen(xasw->name);
  if (strlen(getctxt().grpcfg->openinfo) > 0) {
    if (strncmp(xasw->name, info, len) != 0 || info[len] != ':') {
      userlog("Resource Manager mismatch %s and open info %s", xasw->name,
              info);
      return TX_ERROR;
    }
  }

  auto xarc = xasw->xa_open_entry(info + len + 1, fux::tx::grpno, TMNOFLAGS);
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
  auto xarc = xasw->xa_close_entry(getctxt().grpcfg->closeinfo, fux::tx::grpno,
                                   TMNOFLAGS);
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

static XID xid() {
  auto id = getmib().genuid();

  XID xid;
  // 0 for OSI-CCR
  // >0 for other
  // Use a single-digit value for shorter output
  xid.formatID = 1;
  xid.gtrid_length = sizeof(id);
  xid.bqual_length = 1;
  std::copy_n(reinterpret_cast<char *>(&id), sizeof(id), xid.data);
  xid.data[xid.gtrid_length] = 0;

  return xid;
}

int tx_begin() {
  if (TMREGISTER) {
    // return TX_OK;
  }

  TXINFO info;
  info.xid = xid();
  getctxt().gttid = getmib().transactions().start(info.xid, fux::tx::grpidx);
  return _tx_start(&info);
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
  uint16_t grpno;
  int cd;
  int result;
};

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
  auto &tx = getmib().transactions()[getmib().transactions().find(*xid)];
  for (size_t group = 0; group < max; group++) {
    if (tx.groups[group] > 0) {
      branches.push_back({getmib().groups().at(group).grpno});
    }
  }
  return branches;
}

int _tx_end(TXINFO *info);
int tx_commit() {
  TXINFO info;
  auto rc = _tx_end(&info);
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
  auto rc = _tx_end(&info);
  if (rc != TX_OK) {
    return rc;
  }

  std::vector<branch> branches = collect(&info.xid);

  command(branches, &info.xid, fux::tm::rollback);

  return change_state(TX_OK);
}

static int _tx_end(TXINFO *info, long flags) {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  userlog("xa_end(%s, %d, 0x%0lx) ...",
          fux::to_string(&getctxt().info.xid).c_str(), fux::tx::grpno, flags);
  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, fux::tx::grpno, flags);
  userlog("xa_end(%s, %d, 0x%0lx) = %d",
          fux::to_string(&getctxt().info.xid).c_str(), fux::tx::grpno, flags,
          xarc);
  if (info != nullptr) {
    *info = getctxt().info;
    getctxt().notrx();
  }

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

  return rc;
}

// The main difference is that suspend does not start a new transaction even in
// chained mode
int _tx_suspend(TXINFO *info) {
  auto rc = _tx_end(info, TMSUSPEND);
  if (rc == TX_OK) {
    if (getctxt().state == tx_state::s3) {
      getctxt().state = tx_state::s1;
    } else if (getctxt().state == tx_state::s4) {
      getctxt().state = tx_state::s2;
    }
  }

  return rc;
}

int _tx_end(TXINFO *info) {
  auto rc = _tx_end(info, TMSUCCESS);
  return change_state(rc);
}

int _tx_end(bool ok) {
  TXINFO info;
  auto rc = _tx_end(&info, ok ? TMSUCCESS : TMFAIL);
  return change_state(rc);
}

static int _tx_start(TXINFO *info, long flags) {
  if (!(getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2)) {
    return TX_PROTOCOL_ERROR;
  }

  if (info != nullptr) {
    getctxt().info.xid = info->xid;
  }

  userlog("xa_start(%s, %d, 0x%08lx) ...",
          fux::to_string(&getctxt().info.xid).c_str(), fux::tx::grpno, flags);
  auto xarc = xasw->xa_start_entry(&getctxt().info.xid, fux::tx::grpno, flags);
  userlog("xa_start(%s, %d, 0x%08lx) = %d",
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

int _tx_resume(TXINFO *info) { return _tx_start(info, TMRESUME); }

int _tx_start(TXINFO *info) { return _tx_start(info, TMNOFLAGS); }

namespace fux {
void tx_resume() { _tx_resume(nullptr); }
void tx_suspend() { _tx_suspend(nullptr); }
void tx_end(bool ok) { _tx_end(ok); }
void tx_join(fux::gttid gttid) {
  auto &t = getmib().transactions().join(gttid, fux::tx::grpidx);
  TXINFO info;
  info.xid = t.xid;
  _tx_start(&info, TMJOIN);
}
namespace tx {
fux::gttid gttid() { return getctxt().gttid; }
bool transactional() { return !(getctxt().info.xid.formatID == -1); }
}  // namespace tx
};  // namespace fux

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

int tpacall_group(int grpno, char *svc, char *data, long len, long flags);
void command(std::vector<branch> &branches, XID *xid, int command) {
  fux::fml32buf buf;
  int pending = 0;

  for (auto &p : branches) {
    buf.put(TA_XAFUNC, 0, command);
    buf.put(TA_XID, 0, fux::to_string(xid));
    buf.put(TA_RMID, 0, p.grpno);
    buf.put(TA_FLAGS, 0, 0);
    userlog("Calling TM in group %d for %s", p.grpno,
            fux::to_string(xid).c_str());
    p.cd = tpacall_group(p.grpno, const_cast<char *>(".TM"),
                         reinterpret_cast<char *>(*buf.ptrptr()), 0,
                         TPNOTIME | TPNOTRAN);
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
        p.result = buf.get<long>(TA_XARET, 0);
        pending--;
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////
// TM implementation

static void tm(fux::fml32buf &buf) {
  auto func = buf.get<long>(TA_XAFUNC, 0);
  auto xids = buf.get<std::string>(TA_XID, 0);
  int rmid = buf.get<long>(TA_RMID, 0);
  long flags = buf.get<long>(TA_FLAGS, 0);

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
  buf.put(TA_XARET, 0, ret);
}

extern "C" void TM(TPSVCINFO *svcinfo) {
  fux::fml32buf buf(svcinfo);
  tm(buf);
  fux::tpreturn(TPSUCCESS, 0, buf);
}
