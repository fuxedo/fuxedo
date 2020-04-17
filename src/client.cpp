// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "ipc.h"
#include "mib.h"
#include "misc.h"
#include "trx.h"

#include "resp.h"
#include "svcrepo.h"

#include <thread>

namespace fux {
bool is_server();

struct suspend_guard {
  suspend_guard(bool suspend = true) : suspended(false) {
    if (suspend && fux::tx::transactional()) {
      suspended = true;
      fux::tx_suspend();
    }
  }
  ~suspend_guard() {
    if (suspended) {
      fux::tx_resume();
    }
  }
  bool suspended;
};

}  // namespace fux

class client {
 public:
  client(mib &mibcon)
      : mibcon_(mibcon), repo_(mibcon), blocktime_next(0), blocktime_all(0) {
    auto lock = mibcon_.data_lock();
    client_ = mibcon_.make_accesser(getpid());
    rpid = mibcon_.accessers().at(client_).rpid = fux::ipc::qcreate();
  }

  ~client() {
    auto lock = mibcon_.data_lock();
    fux::ipc::qdelete(mibcon_.accessers().at(client_).rpid);
    mibcon_.accessers().at(client_).invalidate();
  }

  int tpacall(int grpno, const char *svc, char *data, long len,
              long flags) try {
    if (tptypes(data, nullptr, nullptr) == -1) {
      return -1;
    }

    int msqid = repo_.get_queue(grpno, svc);

    rq.set_data(data, len);
    checked_copy(svc, rq->servicename);
    if (flags & TPNOREPLY) {
      rq->replyq = -1;
      rq->cd = 0;
    } else {
      rq->replyq = rpid;
      rq->cd = cds.allocate();
      if (rq->cd == -1) {
        return -1;
      }
    }
    if (!(flags & TPNOTRAN) && fux::tx::transactional()) {
      rq->flags |= TPTRAN;
      rq->gttid = fux::tx::gttid();
    } else {
      rq->flags = 0;
      rq->gttid = fux::bad_gttid;
    }

    if (fux::ipc::qsend(msqid, rq, next_blocktime(), to_flags(flags))) {
      fux::atmi::reset_tperrno();
      return rq->cd;
    }

    if (flags & TPNOBLOCK) {
      TPERROR(TPEBLOCK, "Request queue is full");
    } else {
      TPERROR(TPETIME, "Failed to send within timeout");
    }

    if (rq->cd != 0) {
      cds.release(rq->cd);
    }
    return -1;
  } catch (const std::out_of_range &e) {
    TPERROR(TPENOENT, "Service %s does not exist", svc);
    return -1;
  }

  int tpgetrply(int *cd, char **data, long *len, long flags) {
    if (data == nullptr) {
      TPERROR(TPEINVAL, "data is null");
    }
    if (tptypes(*data, nullptr, nullptr) == -1) {
      return -1;
    }

    while (true) {
      fux::ipc::msg res;

      // Lookup buffered responses first
      bool has_res = false;
      if (flags & TPGETANY) {
        int c = cds.any_buffered();
        if (c != -1) {
          res = std::move(cds.buffered(c));
          has_res = true;
        }
      } else {
        if (cds.check(*cd) == -1) {
          return -1;
        }
        if (cds.is_buffered(*cd)) {
          res = std::move(cds.buffered(*cd));
          has_res = true;
        }
      }

      if (!has_res) {
        mibcon_.accessers().at(client_).rpid_timeout =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(next_blocktime());
        fux::ipc::qrecv(rpid, res, 0, 0);
        mibcon_.accessers().at(client_).rpid_timeout = INVALID_TIME;
        userlog("%s received on %0x", __func__, rpid);
      }

      if (res->cat == fux::ipc::unblock) {
        if (flags & TPGETANY) {
          *cd = 0;
        } else {
          userlog("Received time-out for cd=%d", res->cd);
        }
        TPERROR(TPETIME, "Timeout message received");
        return -1;
      }

      // Did not receive the correct response, try again
      if (!(flags & TPGETANY)) {
        if (*cd != res->cd) {
          cds.buffer(std::move(res));
          continue;
        }
      }

      res.get_data(data);
      tpurcode = res->rcode;
      *cd = res->cd;
      cds.release(*cd);
      if (len != nullptr) {
        *len = res.size_data();
      }
      if (res->rval == TPMINVAL) {
        fux::atmi::reset_tperrno();
        return 0;
      }
      TPERROR(res->rval, "Service failed with %d", res->rval);
      return -1;
    }
  }

  int tpcancel(int cd) {
    if (cds.release(cd) == -1) {
      return -1;
    }
    // TPETRAN
    fux::atmi::reset_tperrno();
    return 0;
  }

  int tpsblktime(int blktime, long flags) {
    long units = flags & 0xf0;
    long what = flags - units;
    long timeout;

    if (units == TPBLK_SECOND) {
      timeout = blktime * 1000;
    } else if (units == TPBLK_MILLISECOND) {
      timeout = blktime;
    } else {
      TPERROR(TPEINVAL, "Invalid flags passed to tpsblktime(..., %ld)", flags);
      return -1;
    }

    if (what == TPBLK_NEXT) {
      blocktime_next = timeout;
    } else if (what == TPBLK_ALL) {
      blocktime_all = timeout;
    } else {
      TPERROR(TPEINVAL, "Invalid flags passed to tpsblktime(..., %ld)", flags);
      return -1;
    }

    fux::atmi::reset_tperrno();
    return 0;
  }

  int tpgblktime(long flags) {
    long units = flags & 0xf0;
    long what = flags - units;
    long timeout;

    if (what == 0) {
      timeout = 0;
    } else if (what == TPBLK_NEXT) {
      timeout = blocktime_next;
    } else if (what == TPBLK_ALL) {
      timeout = blocktime_all;
    } else {
      TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
      return -1;
    }

    if (units == TPBLK_SECOND) {
      timeout /= 1000;
    } else if (units == TPBLK_MILLISECOND) {
      timeout /= 1;
    } else {
      TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
      return -1;
    }

    fux::atmi::reset_tperrno();
    return timeout;
  }

  long next_blocktime() {
    long blocktime = 0;
    if (blocktime_next != 0) {
      blocktime = blocktime_next;
      blocktime_next = 0;
    } else if (blocktime_all != 0) {
      blocktime = blocktime_all;
    } else {
      blocktime = mibcon_.mach().blocktime;
    }
    return blocktime;
  }

  int get_queue(const char *svc) { return repo_.get_queue(-1, svc); }

 private:
  fux::ipc::flags to_flags(long flags) {
    if (flags & TPNOTIME) {
      return fux::ipc::flags::notime;
    } else if (flags & TPNOBLOCK) {
      return fux::ipc::flags::noblock;
    } else {
      return fux::ipc::flags::noflags;
    }
  }

  mib &mibcon_;
  size_t client_;
  service_repository repo_;
  fux::ipc::msg rq;
  int rpid;
  long blocktime_next;
  long blocktime_all;
  responses cds;
};

static thread_local std::shared_ptr<client> current_client;
static client &getclient() {
  if (!current_client.get()) {
    current_client = std::make_shared<client>(getmib());
  }
  return *current_client;
}

int tpacall_internal(int grpno, char *svc, char *data, long len, long flags) {
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpacall(grpno, svc, data, len, flags); }, -1);
}

int tpinit(TPINIT *tpinfo) {
  if (fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from server", __func__);
    return -1;
  }
  fux::atmi::exception_boundary([&] { getclient(); });
  return 0;
}

int tpterm() {
  if (fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from server", __func__);
    return -1;
  }
  fux::atmi::exception_boundary([&] { current_client.reset(); });
  return 0;
}

int tpcancel(int cd) {
  return fux::atmi::exception_boundary([&] { return getclient().tpcancel(cd); },
                                       -1);
}

int tpacall(char *svc, char *data, long len, long flags) {
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpacall(-1, svc, data, len, flags); }, -1);
}

int tpgetrply(int *cd, char **data, long *len, long flags) {
  return fux::atmi::exception_boundary(
      [&] {
        // TODO: if no transaction calls, use false
        fux::suspend_guard g(true);
        return getclient().tpgetrply(cd, data, len, flags);
      },
      -1);
}
int tpcall(char *svc, char *idata, long ilen, char **odata, long *olen,
           long flags) {
  return fux::atmi::exception_boundary(
      [&] {
        fux::suspend_guard g(!(flags & TPNOTRAN));
        int cd = getclient().tpacall(-1, svc, idata, ilen, flags);
        if (cd == -1) {
          return -1;
        }
        return tpgetrply(&cd, odata, olen, flags);
      },
      -1);
}

int tpsblktime(int blktime, long flags) {
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpsblktime(blktime, flags); }, -1);
}

int tpgblktime(long flags) {
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpgblktime(flags); }, -1);
}

int get_queue(const char *svc) { return getclient().get_queue(svc); }
