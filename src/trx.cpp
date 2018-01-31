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

#include <atmi.h>
#include <tx.h>

#include "misc.h"

// Maybe
// TX_ERROR => TPERMERR
// TX_FAIL => TPESYSTEM

int topen() {
  if (false) {
    TPERROR(TPEPROTO, "tx_open called from client");
    return -1;
  }

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
  if (false) {
    TPERROR(TPEPROTO, "tx_close called from client");
    return -1;
  }

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

int tpabort(long flags) {
  if (flags != 0) {
    // or Participant
    TPERROR(TPEPROTO, "flags!=0");
    return -1;
  }

  // TODO: clean call descriptors
  int rc = tx_rollback();

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_ERROR || rc == TX_FAIL) {
    TPERROR(TPERMERR, "tx_rollback()=%d", rc);
  } else if (rc == TX_MIXED) {
    TPERROR(TPEHEURISTIC, "tx_rollback()=%d", rc);
  } else if (rc == TX_HAZARD) {
    TPERROR(TPEHAZARD, "tx_rollback()=%d", rc);
  } else if (rc == TX_COMMITTED) {
    TPERROR(TPEPROTO, "Transaction was committed, tx_rollback()=%d", rc);
  } else if (rc == TX_PROTOCOL_ERROR) {
    TPERROR(TPEPROTO, "tx_rollback()=%d", rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected tx_rollback()=%d", rc);
  }
  return -1;
}

int tpcommit(long flags) {
  if (flags != 0) {
    // or Participant
    TPERROR(TPEPROTO, "flags!=0");
    return -1;
  }

  // TODO: clean call descriptors, do rollback
  int rc = tx_commit();

  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  if (rc == TX_ERROR || rc == TX_FAIL) {
    TPERROR(TPERMERR, "tx_commit()=%d", rc);
  } else if (rc == TX_MIXED) {
    TPERROR(TPEHEURISTIC, "tx_commit()=%d", rc);
  } else if (rc == TX_HAZARD) {
    TPERROR(TPEHAZARD, "tx_commit()=%d", rc);
  } else if (rc == TX_ROLLBACK) {
    TPERROR(TPEABORT, "Transaction was rolled back, tx_commit()=%d", rc);
  } else if (rc == TX_PROTOCOL_ERROR) {
    TPERROR(TPEPROTO, "tx_commit()=%d", rc);
  } else {
    TPERROR(TPESYSTEM, "Unexpected tx_commit()=%d", rc);
  }
  return -1;
}

int _tx_suspend(TXINFO *info);
int tpsuspend(TPTRANID *tranid, long flags) {
  if (tranid == nullptr) {
    TPERROR(TPEPROTO, "tranid==nullptr");
    return -1;
  }
  if (flags != 0) {
    TPERROR(TPEPROTO, "flags!=0");
    return -1;
  }

  TXINFO info;
  int rc = _tx_suspend(&info);
  if (rc == TX_OK) {
    tranid->info[0] = info.xid.formatID;
    tranid->info[1] = info.xid.gtrid_length;
    tranid->info[2] = info.xid.bqual_length;
    if (static_cast<size_t>(info.xid.gtrid_length + info.xid.bqual_length) <
        sizeof(long) * 2) {
      memcpy(&tranid->info[3], info.xid.data,
             info.xid.gtrid_length + info.xid.bqual_length);
    } else {
      TPERROR(TPEINVAL, "Invalid XID, too big");
      return -1;
    }
    fux::atmi::reset_tperrno();
    return 0;
  }

  return -1;
}

int _tx_resume(TXINFO *info);
int tpresume(TPTRANID *tranid, long flags) {
  if (tranid == nullptr) {
    TPERROR(TPEPROTO, "tranid==nullptr");
    return -1;
  }
  if (flags != 0) {
    TPERROR(TPEPROTO, "flags!=0");
    return -1;
  }

  TXINFO info;
  info.xid.formatID = tranid->info[0];
  info.xid.gtrid_length = tranid->info[1];
  info.xid.bqual_length = tranid->info[2];
  if (static_cast<size_t>(info.xid.gtrid_length + info.xid.bqual_length) <
      sizeof(long) * 2) {
    memcpy(info.xid.data, &tranid->info[3],
           info.xid.gtrid_length + info.xid.bqual_length);
  } else {
    TPERROR(TPEINVAL, "Invalid XID, too big");
    return -1;
  }

  int rc = _tx_resume(&info);
  if (rc == TX_OK) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  return -1;
}

int tpbegin(unsigned long timeout, long flags) {
  if (flags != 0) {
    TPERROR(TPEPROTO, "flags!=0");
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
