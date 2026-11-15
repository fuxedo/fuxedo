// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xatmi.h>

#include <atmi.h>

#include "misc.h"
#include "qm.h"
#include "trx.h"

namespace {

int qm_fail(TPQCTL *ctl, const fux::qm::qm_error &e) {
  if (ctl != nullptr) {
    ctl->diagnostic = e.diagnostic;
  }
  if (e.diagnostic == QMEINVAL) {
    TPERROR(TPEINVAL, "%s", e.what());
  } else if (e.diagnostic == QMEBADQUEUE || e.diagnostic == QMEBADMSGID ||
             e.diagnostic == QMENOMSG) {
    TPERROR(TPENOENT, "%s", e.what());
  } else {
    TPERROR(TPEOS, "%s", e.what());
  }
  return -1;
}

int not_in_transaction() {
  if (fux::tx::tx_active()) {
    TPERROR(TPETRAN, "queue operations in transaction mode not supported yet");
    return -1;
  }
  return 0;
}

}  // namespace

int tpenqueue(char *qspace, char *qname, TPQCTL *ctl, char *data, long len,
              long flags) {
  if (qspace == nullptr || qname == nullptr || ctl == nullptr) {
    TPERROR(TPEINVAL, "qspace, qname and ctl are required");
    return -1;
  }
  if (flags != TPNOFLAGS) {
    TPERROR(TPEINVAL, "invalid flags %ld", flags);
    return -1;
  }
  if (not_in_transaction() == -1) {
    if (ctl != nullptr) {
      ctl->diagnostic = QMETRAN;
    }
    return -1;
  }

  try {
    std::string payload;
    if (data != nullptr) {
      long used = fux::mem::bufsize(data, len);
      if (used == -1) {
        return -1;
      }
      payload.resize(used);
      if (tpexport(data, len, &payload[0], &used, 0) == -1) {
        return -1;
      }
      payload.resize(used);
    }

    auto id = fux::qm::enqueue(qspace, qname, ctl, payload);
    if ((ctl->flags & TPQMSGID) != 0) {
      memset(ctl->msgid, 0, TMMSGIDLEN);
      std::copy_n(id.c_str(), std::min(id.size(), (size_t)TMMSGIDLEN - 1),
                  ctl->msgid);
    }
    fux::atmi::reset_tperrno();
    return 0;
  } catch (const fux::qm::qm_error &e) {
    return qm_fail(ctl, e);
  } catch (const std::exception &e) {
    TPERROR(TPESYSTEM, "%s", e.what());
    return -1;
  }
}

int tpdequeue(char *qspace, char *qname, TPQCTL *ctl, char **data, long *len,
              long flags) {
  constexpr long valid_flags = TPQGETBYMSGID | TPQGETBYCORRID | TPQPEEK |
                               TPQWAIT | TPQTIME_ABS | TPQTIME_REL;
  if (qspace == nullptr || qname == nullptr || ctl == nullptr ||
      data == nullptr) {
    TPERROR(TPEINVAL, "qspace, qname, ctl and data are required");
    return -1;
  }
  if ((flags & ~valid_flags) != 0) {
    TPERROR(TPEINVAL, "invalid flags %ld", flags);
    return -1;
  }
  if (*data == nullptr) {
    TPERROR(TPEINVAL, "*data must point to a buffer allocated with tpalloc");
    return -1;
  }
  if (not_in_transaction() == -1) {
    ctl->diagnostic = QMETRAN;
    return -1;
  }

  try {
    long wait_ms = (flags & TPQWAIT) ? 30000 : 0;
    auto result = fux::qm::dequeue(qspace, qname, ctl,
                                   (flags & TPQPEEK) != 0, wait_ms);
    if (!result.found) {
      ctl->diagnostic = QMENOMSG;
      TPERROR(TPETIME, "no message available on queue %s", qname);
      return -1;
    }

    if (!result.m.payload.empty()) {
      if (tpimport(&result.m.payload[0], result.m.payload.size(), data, 0,
                   0) == -1) {
        return -1;
      }
    }
    if (len != nullptr) {
      *len = result.m.payload.size();
    }

    if ((ctl->flags & TPQMSGID) != 0) {
      memset(ctl->msgid, 0, TMMSGIDLEN);
      std::copy_n(result.m.id.c_str(),
                  std::min(result.m.id.size(), (size_t)TMMSGIDLEN - 1),
                  ctl->msgid);
    }
    if ((ctl->flags & TPQCORRID) != 0) {
      memset(ctl->corrid, 0, TMCORRIDLEN);
      std::copy_n(result.m.corrid.data(),
                  std::min(result.m.corrid.size(), (size_t)TMCORRIDLEN),
                  ctl->corrid);
    }
    if ((ctl->flags & TPQPRIORITY) != 0) {
      ctl->priority = result.m.priority;
    }
    fux::atmi::reset_tperrno();
    return 0;
  } catch (const fux::qm::qm_error &e) {
    return qm_fail(ctl, e);
  } catch (const std::exception &e) {
    TPERROR(TPESYSTEM, "%s", e.what());
    return -1;
  }
}
