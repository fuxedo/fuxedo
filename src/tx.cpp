// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <atmi.h>
#include <tx.h>
#include <xa.h>
#include <cstdint>
#include <memory>

#include <algorithm>

#include "mib.h"

extern "C" {
extern struct xa_switch_t tmnull_switch;
}

static xa_switch_t *xasw = &tmnull_switch;

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
  tx_context(mib &mibcon) : mibcon_(mibcon) {}
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

int tx_open() {
  auto xarc = xasw->xa_open_entry(getctxt().grpcfg->openinfo,
                                  getctxt().grpcfg->grpno, TMNOFLAGS);
  if (xarc == XA_OK) {
    if (getctxt().state == tx_state::s0) {
      getctxt().state = tx_state::s1;
    }
    return TX_OK;
  }

  if (xarc == XAER_RMERR) {
    return TX_ERROR;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  }

  return TX_FAIL;
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

  if (xarc == XAER_RMERR) {
    return TX_ERROR;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  }

  return TX_FAIL;
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
  xid->formatID = 186;
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
    return TX_OK;
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

int tx_commit() {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMNOFLAGS);
  if (xarc == XA_OK) {
    // pass
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  } else {
    return TX_FAIL;
  }

  xarc = xasw->xa_commit_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                               TMNOFLAGS);
  if (xarc == XA_HEURHAZ) {
    return TX_HAZARD;
  } else if (xarc == XA_HEURMIX) {
    return TX_MIXED;
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  }
  return TX_FAIL;
}

int tx_rollback() {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMNOFLAGS);
  if (xarc == XA_OK) {
    // pass
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  } else {
    return TX_FAIL;
  }

  xarc = xasw->xa_rollback_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMNOFLAGS);
  if (xarc == XA_HEURHAZ) {
    return TX_HAZARD;
  } else if (xarc == XA_HEURMIX) {
    return TX_MIXED;
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  }
  return TX_FAIL;

  return -1;
}

int _tx_suspend(TXINFO *info) {
  if (!(getctxt().state == tx_state::s3 || getctxt().state == tx_state::s4)) {
    return TX_PROTOCOL_ERROR;
  }

  auto xarc = xasw->xa_end_entry(&getctxt().info.xid, getctxt().grpcfg->grpno,
                                 TMSUSPEND);
  if (xarc == XA_OK) {
    // pass
  } else if (xarc == XAER_RMFAIL) {
    return TX_FAIL;
  } else if (xarc == XAER_INVAL) {
    return TX_FAIL;
  } else if (xarc == XAER_PROTO) {
    return TX_FAIL;
  } else {
    return TX_FAIL;
  }

  memcpy(info, &(getctxt().info), sizeof(*info));
  getctxt().info.xid.formatID = -1;
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
