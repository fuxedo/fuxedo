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
#include "probes.h"
#include "trx.h"
#include "qm.h"
#include "tlog.h"

extern "C" {
extern struct xa_switch_t tmnull_switch;
}

namespace fux {
bool is_server();
}

namespace fux::tx {
uint16_t grpno = 0;
size_t grpidx = 0;
}  // namespace fux::tx

namespace fux::glob {
xa_switch_t *xasw = &tmnull_switch;
}

using fux::glob::xasw;

// The null switch marks "no real RM". Clients own no resource manager:
// they coordinate branches that live in other processes through the
// groups' .TM services, the way the platform runs client-side
// transactions.
static bool local_rm() { return xasw != &tmnull_switch; }

static void recover_tlog();

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
    // Clients own no RM and never call tx_open; their context starts
    // opened, matching the platform's client-side transactions.
    state = xasw == &tmnull_switch ? tx_state::s1 : tx_state::s0;
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
  // set by tx_close: a thread whose RM was closed on purpose must not
  // be promoted out of S0 by the worker-thread begin path
  bool rm_closed = false;
  group *grpcfg = nullptr;
  TXINFO info;
  fux::gttid gttid;
  // transaction lifetime: set at begin from the requested timeout
  std::chrono::steady_clock::time_point deadline;
  bool has_deadline = false;
  // set when the current transaction was begun by an explicit tpbegin
  // inside a service; such a transaction must not outlive tpreturn
  // (pinned: Oracle answers the caller with TPESVCERR and aborts it)
  bool own_begin = false;
  // set when AUTOTRAN began the transaction around this request; the
  // platform (not the service) drives its commit at tpreturn
  bool autotran_begin = false;
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
static int rm_err(int xarc, const char * /*func*/) {
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
  if (!local_rm()) {
    getctxt().state = tx_state::s1;
    return TX_OK;
  }
  char *info = getctxt().grpcfg->openinfo;
  auto len = strlen(xasw->name);
  if (strlen(getctxt().grpcfg->openinfo) > 0) {
    if (strncmp(xasw->name, info, len) != 0 || info[len] != ':') {
      userlog("Resource Manager mismatch %s and open info %s", xasw->name,
              info);
      return TX_ERROR;
    }
  }

  auto xarc = xasw->xa_open_entry(info + len + 1, 0, TMNOFLAGS);
  FUX_PROBE3(xa__open, xasw->name, fux::tx::grpno, xarc);
  if (xarc == XA_OK) {
    getctxt().rm_closed = false;
    if (getctxt().state == tx_state::s0) {
      getctxt().notrx();
      getctxt().state = tx_state::s1;
      try {
        recover_tlog();
      } catch (const std::exception &e) {
        userlog("TLOG recovery failed: %s", e.what());
      }
    }
    return TX_OK;
  }

  return rm_err(xarc, "xa_open");
}

int tx_close() {
  if (!local_rm()) {
    getctxt().state = tx_state::s0;
    getctxt().rm_closed = true;
    return TX_OK;
  }
  if (!(getctxt().state == tx_state::s0 || getctxt().state == tx_state::s1 ||
        getctxt().state == tx_state::s2)) {
    return TX_PROTOCOL_ERROR;
  }
  auto xarc = xasw->xa_close_entry(getctxt().grpcfg->closeinfo, 0,
                                   TMNOFLAGS);
  FUX_PROBE2(xa__close, xasw->name, xarc);
  if (xarc == XA_OK) {
    getctxt().state = tx_state::s0;
    getctxt().rm_closed = true;
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

namespace fux::tx {
bool expired() {
  auto &ctxt = getctxt();
  return ctxt.has_deadline &&
         std::chrono::steady_clock::now() >= ctxt.deadline;
}
long time_left_ms() {
  auto &ctxt = getctxt();
  if (!ctxt.has_deadline) {
    return -1;
  }
  auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                  ctxt.deadline - std::chrono::steady_clock::now())
                  .count();
  return left < 0 ? 0 : left;
}
}  // namespace fux::tx

int tx_begin() {
  if (TMREGISTER) {
    // return TX_OK;
  }

  // A thread that never opened its own context (TMQFORWARD's
  // forwarding threads, for example) starts in S0 per the XA state
  // machine, but the process RM is open already and such a thread
  // drives no local branch; treat it as opened. A thread that closed
  // its RM on purpose keeps failing with PROTO, the platform pin.
  if (getctxt().state == tx_state::s0 && !getctxt().rm_closed) {
    getctxt().state = tx_state::s1;
  }

  // a begin issued while dispatching a service (rather than by the
  // autotran machinery before it, or by a client) leaves the
  // transaction open at tpreturn, which the platform rejects
  getctxt().own_begin = fux::is_server();

  TXINFO info = {};
  info.xid = xid();
  // a caller without a group (a client) starts the transaction
  // unaffiliated; group bits are added when a service call joins it.
  // Joining unconditionally would stamp group index 0 and make the
  // commit drive a TMS that was never part of the transaction
  getctxt().gttid = fux::tx::grpno != 0
                        ? getmib().transactions().start(info.xid,
                                                        fux::tx::grpidx)
                        : getmib().transactions().start(info.xid);
  auto rc = _tx_start(&info);
  if (rc == TX_OK) {
    // the requested timeout lives in the context (tpbegin sets it via
    // tx_set_transaction_timeout before begin; AUTOTRAN sets it from
    // the service TRANTIME)
    auto timeout = getctxt().info.transaction_timeout;
    if (timeout > 0) {
      getctxt().deadline = std::chrono::steady_clock::now() +
                           std::chrono::seconds(timeout);
      getctxt().has_deadline = true;
    } else {
      getctxt().has_deadline = false;
    }
  }
  return rc;
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

static fux::tlog::log *tlog_for(const char *purpose) {
  static std::string device = getmib().mach().tlogdevice;
  if (device.empty()) {
    return nullptr;
  }
  static fux::tlog::log log = [] {
    try {
      return fux::tlog::log::open(device);
    } catch (const std::exception &e) {
      userlog("TLOG %s unavailable, transaction outcomes are not logged: %s",
              device.c_str(), e.what());
      return fux::tlog::log{};
    }
  }();
  (void)purpose;
  static bool unusable = log.size() == 0;
  return unusable ? nullptr : &log;
}

// The decision to commit is written to the TLOG before the second phase
// runs, so a crash can be resolved during recovery (presumed abort).
static void persist_tlog(XID *xid, std::vector<branch> branches) {
  auto *log = tlog_for("commit");
  if (log != nullptr && !branches.empty()) {
    log->append(fux::tlog::record_type::commit, xid);
  }
}

static void clean_tlog(XID * /*xid*/) {
  // outcome records stay in the circular log for inspection (crlog)
}

static void recover_tlog() {
  auto *log = tlog_for("recovery");
  if (log == nullptr) {
    return;
  }
  XID xids[64];
  // the platform opens, starts, and recovers with rmid 0 in every
  // process (verified in its xa_NULLtrc); rmid is a process-local
  // handle, so 0 is safe and matches the pinned convention
  int n = xasw->xa_recover_entry(xids, 64, 0, TMSTARTRSCAN);
  FUX_PROBE1(xa__recover, n);
  for (int i = 0; i < n; i++) {
    int outcome = log->outcome(&xids[i]);
    if (outcome == 1) {
      userlog("recovering in-doubt transaction %s with COMMIT",
              fux::to_string(&xids[i]).c_str());
      FUX_PROBE2(heuristic__resolve, fux::to_string(&xids[i]).c_str(), 1);
      xasw->xa_commit_entry(&xids[i], 0, TMNOFLAGS);
    } else if (outcome == 3) {
      // resolved heuristically before this boot; nothing to redo
      userlog("in-doubt transaction %s was completed heuristically",
              fux::to_string(&xids[i]).c_str());
      FUX_PROBE2(heuristic__resolve, fux::to_string(&xids[i]).c_str(), 3);
      xasw->xa_forget_entry(&xids[i], 0, TMNOFLAGS);
    } else {
      FUX_PROBE2(heuristic__resolve, fux::to_string(&xids[i]).c_str(), 0);
      if (fux::qm::qm_branch_staged_empty(&xids[i])) {
        // prepared but the effects are already gone and no decision was
        // ever logged: someone completed this branch outside the log
        userlog("in-doubt transaction %s completed heuristically",
                fux::to_string(&xids[i]).c_str());
        log->append(fux::tlog::record_type::heuristic, &xids[i]);
      } else {
        userlog("recovering in-doubt transaction %s with ROLLBACK",
                fux::to_string(&xids[i]).c_str());
      }
      xasw->xa_rollback_entry(&xids[i], 0, TMNOFLAGS);
    }
  }
}

static std::vector<branch> collect(XID *xid) {
  std::vector<branch> branches;
  auto max = getmib().transactions().max_groups;
  auto &tx = getmib().transactions()[getmib().transactions().find(*xid)];
  for (size_t group = 0; group < max; group++) {
    if (tx.groups[group] > 0) {
      branches.push_back({getmib().groups().at(group).grpno, 0, 0});
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

  // The classic two-phase sequence for the thread's transaction; agents
  // (the workstation listener) drive other transactions through
  // fux::tm::drive() the same way.
  if (fux::tm::drive(&info.xid)) {
    return change_state(TX_OK);
  }
  return change_state(TX_FAIL);
}

namespace fux::tm {
// The full two-phase sequence for xid against every group that joined
// it in the shared transaction table: prepare, decide, persist the
// decision to the machine TLOG, run the second phase. Returns true when
// committed, false when rolled back. Used by tx_commit and by
// transaction agents such as the workstation listener.
bool drive(const XID *xid) {
  std::vector<branch> branches1 = collect(const_cast<XID *>(xid));
  command(branches1, const_cast<XID *>(xid), fux::tm::prepare);

  std::vector<branch> branches2;
  bool commit = true;
  for (auto &p : branches1) {
    if (p.result == XA_RDONLY) {
      continue;
    }
    if (p.result != XA_OK) {
      commit = false;
    }
    branches2.push_back(p);
  }

  if (commit) {
    persist_tlog(const_cast<XID *>(xid), branches2);
  }

  if (commit) {
    command(branches2, const_cast<XID *>(xid), fux::tm::commit);
    clean_tlog(const_cast<XID *>(xid));
  } else {
    command(branches2, const_cast<XID *>(xid), fux::tm::rollback);
  }
  // the transaction is decided; give its shared table entry back.
  // Agents (WSL, GWTDOMAIN) also end their own view, which is harmless
  // - end() is a memset - but the core path must do it or every
  // transaction leaks an entry until the table fills and the next
  // begin aborts the server (pinned: bankapp populate, 2026-11-22)
  try {
    XID mutable_xid = *xid;
    getmib().transactions().end(getmib().transactions().find(mutable_xid));
  } catch (const std::out_of_range &) {
  }
  return commit;
}
}  // namespace fux::tm

int tx_rollback() {
  TXINFO info;
  auto rc = _tx_end(&info);
  if (rc != TX_OK) {
    return rc;
  }

  std::vector<branch> branches = collect(&info.xid);

  auto *log = tlog_for("rollback");
  if (log != nullptr && !branches.empty()) {
    log->append(fux::tlog::record_type::rollback, &info.xid);
  }

  command(branches, &info.xid, fux::tm::rollback);

  try {
    getmib().transactions().end(getmib().transactions().find(info.xid));
  } catch (const std::out_of_range &) {
  }

  return change_state(TX_OK);
}

static int _tx_end(TXINFO *info, long flags) {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  if (!local_rm()) {
    if (info != nullptr) {
      *info = getctxt().info;
      getctxt().notrx();
    }
    return TX_OK;
  }

  auto gtrid = fux::to_string(&getctxt().info.xid);
  FUX_PROBE3(xa__end, gtrid.c_str(), fux::tx::grpno, flags);
  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, 0, flags);
  FUX_PROBE1(xa__end_return, xarc);
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

  if (!local_rm()) {
    getctxt().state = getctxt().state == tx_state::s1 ? tx_state::s3
                                                      : tx_state::s4;
    return TX_OK;
  }

  auto gtrid = fux::to_string(&getctxt().info.xid);
  FUX_PROBE3(xa__start, gtrid.c_str(), fux::tx::grpno, flags);
  auto xarc = xasw->xa_start_entry(&getctxt().info.xid, 0, flags);
  FUX_PROBE1(xa__start_return, xarc);
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
bool tx_end(bool ok) { return _tx_end(ok) == TX_OK; }
void tx_join(fux::gttid gttid) {
  auto &tbl = getmib().transactions();
  // first touch of this RM branch is a fresh start; later joins (other
  // servers of the same group, or suspend/resume pairs) use TMJOIN
  bool first = !tbl.has_joined(gttid, fux::tx::grpidx);
  auto &t = tbl.join(gttid, fux::tx::grpidx);
  getctxt().gttid = gttid;
  TXINFO info;
  info.xid = t.xid;
  _tx_start(&info, first ? TMNOFLAGS : TMJOIN);
}
fux::gttid tx_adopt(const XID &xid) {
  auto &tbl = getmib().transactions();
  fux::gttid id;
  XID copy = xid;
  try {
    id = tbl.find(copy);
  } catch (const std::out_of_range &) {
    id = tbl.start(xid);
  }
  getctxt().info.xid = xid;
  getctxt().gttid = id;
  if (getctxt().state == tx_state::s1 || getctxt().state == tx_state::s2) {
    getctxt().state = tx_state::s3;
  }
  return id;
}
void tx_detach() {
  getctxt().notrx();
  if (getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4) {
    getctxt().state = tx_state::s1;
  }
}
namespace tx {
fux::gttid gttid() { return getctxt().gttid; }
bool own_begin() { return getctxt().own_begin; }
void clear_own_begin() { getctxt().own_begin = false; }
bool autotran_begin() { return getctxt().autotran_begin; }
void set_autotran_begin(bool v) { getctxt().autotran_begin = v; }
bool transactional() { return !(getctxt().info.xid.formatID == -1); }
// Lazy variant: does not create a transaction context (and does not require
// an attached domain) when the thread has not touched transactions at all.
XID current_xid() { return getctxt().info.xid; }

bool tx_active() {
  return txctxt.get() != nullptr && txctxt->info.xid.formatID != -1;
}
}  // namespace tx
};  // namespace fux

int fux::tm::coordinate(const XID *xid, int op) {
  FUX_PROBE2(tx__coordinate, fux::to_string(xid).c_str(), op);
  std::vector<branch> branches = collect(const_cast<XID *>(xid));
  if (branches.empty()) {
    return XA_RDONLY;
  }
  command(branches, const_cast<XID *>(xid), op);
  if (op != fux::tm::prepare) {
    return XA_OK;
  }
  bool any = false;
  for (auto &p : branches) {
    if (p.result == XA_OK) {
      any = true;
    } else if (p.result != XA_RDONLY) {
      return p.result;
    }
  }
  auto vote = any ? XA_OK : XA_RDONLY;
  FUX_PROBE1(tx__coordinate_return, vote);
  return vote;
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
  // a worker thread that never opened its own context (TMQFORWARD's
  // forwarding threads, for example) starts in S0 per the XA state
  // machine, but the process RM is open already and such a thread
  // drives no local branch; treat it as opened, unless the thread
  // closed its RM on purpose (the platform pin: begin after tpclose
  // fails)
  if (getctxt().state == tx_state::s0 && !getctxt().rm_closed) {
    getctxt().state = tx_state::s1;
  }
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
// the TM operation itself, shared by the .TM service and the local
// branch drive below
static int tm_op(int func, XID *xid, int rmid, long flags) {
  int ret;
  switch (func) {
    case fux::tm::prepare:
      FUX_PROBE3(xa__prepare, fux::to_string(xid).c_str(), rmid, flags);
      ret = xasw->xa_prepare_entry(xid, rmid, flags);
      FUX_PROBE1(xa__prepare_return, ret);
      return ret;
    case fux::tm::commit:
      FUX_PROBE3(xa__commit, fux::to_string(xid).c_str(), rmid, flags);
      ret = xasw->xa_commit_entry(xid, rmid, flags);
      FUX_PROBE1(xa__commit_return, ret);
      return ret;
    case fux::tm::rollback:
      FUX_PROBE3(xa__rollback, fux::to_string(xid).c_str(), rmid, flags);
      ret = xasw->xa_rollback_entry(xid, rmid, flags);
      FUX_PROBE1(xa__rollback_return, ret);
      return ret;
    default:
      return XAER_INVAL;
  }
}

void command(std::vector<branch> &branches, XID *xid, int command) {
  fux::fml32buf buf;

  // the local group's branch is driven in-process: the branch's
  // xa_start ran on this process's connection, so prepare/commit must
  // run here too. forwarding it to the TMS would ask a different
  // session about a branch it never saw (the platform's TMS handles
  // recovery and the branches of servers that have left)
  for (auto &p : branches) {
    if (fux::is_server() && p.grpno == fux::tx::grpno) {
      p.result = tm_op(command, xid, 0, 0);
      p.cd = -1;
    }
  }

  for (auto &p : branches) {
    if (p.cd == -1) {
      continue;
    }
    buf.put(TA_XAFUNC, 0, command);
    buf.put(TA_XID, 0, fux::to_string(xid));
    buf.put(TA_RMID, 0, 0);
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
      return;
    }
  }

  // Wait for each branch's own reply; TPGETANY is unsafe here because a
  // server that pumps its socket engine while waiting (the domain
  // gateway) can re-enter this function nested and would steal the
  // outer call's reply from the shared queue.
  for (auto &p : branches) {
    if (p.cd == -1) {
      continue;
    }
    int cd = p.cd;
    long len = 0;
    if (tpgetrply(&cd, reinterpret_cast<char **>(buf.ptrptr()), &len, 0) ==
        -1) {
      userlog("TM reply from group %d failed (%s)", p.grpno,
              tpstrerror(tperrno));
      p.result = TMER_TMERR;
      continue;
    }
    p.result = buf.get<long>(TA_XARET, 0);
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
  buf.put(TA_XARET, 0, tm_op(static_cast<int>(func), &xid, rmid, flags));
}

extern "C" void TM(TPSVCINFO *svcinfo) {
  fux::fml32buf buf(svcinfo);
  tm(buf);
  fux::tpreturn(TPSUCCESS, 0, buf);
}
