// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
// This implements the Oracle Tuxedo transaction API by calling TX APIs

#include <atmi.h>
#include <tx.h>

#include "ipc.h"
#include "misc.h"
#include "trx.h"

// Maybe
// TX_ERROR => TPERMERR
// TX_FAIL => TPESYSTEM

int tpopen() {
  int rc = tx_open();

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_ERROR || rc == TX_FAIL) {
    TPERROR(TPERMERR, "tx_open()=%d", rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected tx_open()=%d", rc);
  }
  return -1;
}

int tpclose() {
  int rc = tx_close();

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_ERROR || rc == TX_FAIL) {
    TPERROR(TPERMERR, "tx_close()=%d", rc);
  } else if (rc == TX_PROTOCOL_ERROR) {
    TPERROR(TPEPROTO, "tx_close()=%d", rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected tx_close()=%d", rc);
  }
  return -1;
}

int tpbegin(unsigned long timeout, long flags) {
  if (flags != 0) {
    TPERROR(TPEINVAL, "flags!=0");
    return -1;
  }

  int rc = tx_set_transaction_timeout(timeout);
  if (rc == TX_OK) {
    rc = tx_begin();
  }

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_ERROR || rc == TX_FAIL) {
    TPERROR(TPERMERR, "tx_begin()=%d", rc);
  } else if (rc == TX_OUTSIDE) {
    TPERROR(TPETRAN, "tx_begin()=%d", rc);
  } else if (rc == TX_PROTOCOL_ERROR) {
    TPERROR(TPEPROTO, "tx_begin()=%d", rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected tx_begin()=%d", rc);
  }
  return -1;
}

int tpgetlev() {
  int rc = tx_info(nullptr);

  if (rc == 0 || rc == 1) {
    fux::atmi::reset_tperrno();
    return rc;
  }

  if (rc == TX_FAIL) {
    TPERROR(TPESYSTEM, "tx_info()=%d", rc);
  } else if (rc == TX_PROTOCOL_ERROR) {
    TPERROR(TPEPROTO, "tx_info()=%d", rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected tx_info()=%d", rc);
  }
  return -1;
}

static int trx_err(int rc, const char *func) {
  if (rc == TX_ERROR || rc == TX_FAIL) {
    TPERROR(TPERMERR, "%s()=%d", func, rc);
  } else if (rc == TX_MIXED) {
    TPERROR(TPEHEURISTIC, "%s()=%d", func, rc);
  } else if (rc == TX_HAZARD) {
    TPERROR(TPEHAZARD, "%s()=%d", func, rc);
  } else if (rc == TX_PROTOCOL_ERROR) {
    TPERROR(TPEPROTO, "%s()=%d", func, rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected %s()=%d", func, rc);
  }
  return -1;
}

int tpabort(long flags) {
  if (flags != 0) {
    TPERROR(TPEINVAL, "flags!=0");
    return -1;
  }

  // TODO: clean call descriptors
  int rc = tx_rollback();

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_COMMITTED) {
    TPERROR(TPEPROTO, "Transaction was committed, tx_rollback()=%d", rc);
    return -1;
  }
  return trx_err(rc, "tx_rollback");
}

int tpcommit(long flags) {
  if (flags != 0) {
    TPERROR(TPEINVAL, "flags!=0");
    return -1;
  }

  // TODO: clean call descriptors, do rollback
  int rc = tx_commit();

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_ROLLBACK) {
    TPERROR(TPEABORT, "Transaction was rolled back, tx_commit()=%d", rc);
    return -1;
  }
  return trx_err(rc, "tx_commit");
}

int tpsuspend(TPTRANID *tranid, long flags) {
  if (tranid == nullptr) {
    TPERROR(TPEINVAL, "tranid==nullptr");
    return -1;
  }
  if (flags != 0) {
    TPERROR(TPEINVAL, "flags!=0");
    return -1;
  }

  TXINFO info;
  int rc = _tx_suspend(&info);
  if (rc == TX_OK) {
    if (sizeof(*tranid) <
        offsetof(XID, data) + info.xid.gtrid_length + info.xid.bqual_length) {
      TPERROR(TPEINVAL, "Invalid XID, too big");
      return -1;
    }
    std::copy_n(
        reinterpret_cast<char *>(&info.xid),
        offsetof(XID, data) + info.xid.gtrid_length + info.xid.bqual_length,
        reinterpret_cast<char *>(&tranid->info));
    fux::atmi::reset_tperrno();
    return 0;
  }

  return trx_err(rc, "tpsuspend");
}

int tpresume(TPTRANID *tranid, long flags) {
  if (tranid == nullptr) {
    TPERROR(TPEINVAL, "tranid==nullptr");
    return -1;
  }
  if (flags != 0) {
    TPERROR(TPEINVAL, "flags!=0");
    return -1;
  }

  if (sizeof(*tranid) > sizeof(XID)) {
    TPERROR(TPEINVAL, "Invalid TRANID, too big");
    return -1;
  }
  TXINFO info;
  std::copy_n(reinterpret_cast<char *>(&tranid->info), sizeof(*tranid),
              reinterpret_cast<char *>(&info.xid));

  int rc = _tx_resume(&info);
  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  return trx_err(rc, "_tx_start");
}
