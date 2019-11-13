// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>
#include <xatmi.h>

#include <algorithm>
#include <clara.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "calldesc.h"
#include "ctxt.h"
#include "ipc.h"
#include "mib.h"
#include "misc.h"


namespace fux {
  bool is_server();
}

struct service_entry {
  std::vector<int> queues;
  size_t current_queue;
  uint64_t cached_revision;
  uint64_t *mib_revision;
  size_t mib_service;

  service_entry() : current_queue(0) {}
};

class service_repository {
 public:
  service_repository(mib &m) : m_(m) {}
  int get_queue(const char *svc) {
    auto &entry = get_entry(svc);
    refresh(entry);
    return load_balance(entry);
  }

 private:
  service_entry &get_entry(const char *service_name) {
    auto it = services_.find(service_name);
    if (it == services_.end()) {
      auto lock = m_.data_lock();
      service_entry entry;
      entry.mib_service = m_.find_service(service_name);
      if (entry.mib_service == m_.badoff) {
        throw std::out_of_range(service_name);
      }
      entry.mib_revision = &(m_.services().at(entry.mib_service).revision);
      entry.cached_revision = 0;
      it = services_.insert(std::make_pair(service_name, entry)).first;
    }
    return it->second;
  }

  void refresh(service_entry &entry) {
    if (entry.cached_revision != *(entry.mib_revision)) {
      entry.queues.clear();
      auto lock = m_.data_lock();
      auto adv = m_.advertisements();
      for (size_t i = 0; i < adv->len; i++) {
        auto &a = adv.at(i);
        if (a.service == entry.mib_service) {
          auto msqid = m_.queues().at(a.queue).msqid;
          entry.queues.push_back(msqid);
        }
      }
      entry.cached_revision = *(entry.mib_revision);
    }
  }

  int load_balance(service_entry &entry) {
    if (entry.queues.empty()) {
      throw std::out_of_range("no queue");
    }
    entry.current_queue = (entry.current_queue + 1) % entry.queues.size();
    return entry.queues[entry.current_queue];
  }

  mib &m_;
  std::map<std::string, service_entry, std::less<void>> services_;
};

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
    mibcon_.accessers().at(client_).rpid = -1;
  }

  int tpacall(const char *svc, char *data, long len, long flags) try {
    int msqid = repo_.get_queue(svc);

    auto trx = fux::glob::calldescs()->allocate();
    if (trx.cd() == -1) {
      return -1;
    }

    rq.set_data(data, len);
    checked_copy(svc, rq->servicename);
    rq->mtype = trx.cd();
    rq->cd = trx.cd();
    if (flags & TPNOREPLY) {
      rq->replyq = -1;
    } else {
      rq->replyq = rpid;
    }

    if (fux::ipc::qsend(msqid, rq, next_blocktime(), to_flags(flags))) {
      trx.commit();
      fux::atmi::reset_tperrno();
      return trx.cd();
    }

    return -1;
  } catch (const std::out_of_range &e) {
    TPERROR(TPENOENT, "Service %s does not exist", svc);
    return -1;
  }

  int tpgetrply(int *cd, char **data, long *len, long flags) {
    fux::ipc::msg res;

    if (flags & TPGETANY) {
      fux::ipc::qrecv(rpid, res, 0, 0);
    } else {
      if (fux::glob::calldescs()->check(*cd) == -1) {
        return -1;
      }
      fux::ipc::qrecv(rpid, res, *cd, 0);
    }
    if (res->cat == fux::ipc::unblock) {
      TPERROR(TPETIME, "Timeout message received");
      return -1;
    }

    res.get_data(data);
    tpurcode = res->rcode;
    *cd = res->cd;
    fux::glob::calldescs()->release(*cd);
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

  int tpcancel(int cd) {
    if (fux::glob::calldescs()->release(cd) == -1) {
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
      blocktime = 15 * 1000;
    }
    return blocktime;
  }

  int get_queue(const char *svc) { return repo_.get_queue(svc); }

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
  std::vector<fux::ipc::msg> responses;
  int rpid;
  long blocktime_next;
  long blocktime_all;
};

static thread_local std::shared_ptr<client> current_client;
static client &getclient() {
  if (!current_client.get()) {
    current_client = std::make_shared<client>(getmib());
  }
  return *current_client;
}

int tpinit(TPINIT *tpinfo) {
  if (fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from server", __func__);
    return -1;
  }
  getclient();
  fux::atmi::reset_tperrno();
  return 0;
}

int tpterm() {
  if (fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from server", __func__);
    return -1;
  }
  current_client.reset();
  fux::atmi::reset_tperrno();
  return 0;
}

int tpcancel(int cd) { return getclient().tpcancel(cd); }

int tpacall(char *svc, char *data, long len, long flags) {
  return getclient().tpacall(svc, data, len, flags);
}

int tpgetrply(int *cd, char **data, long *len, long flags) {
  return getclient().tpgetrply(cd, data, len, flags);
}
int tpcall(char *svc, char *idata, long ilen, char **odata, long *olen,
           long flags) {
  int cd = tpacall(svc, idata, ilen, flags);
  if (cd == -1) {
    return -1;
  }
  return tpgetrply(&cd, odata, olen, flags);
}

int tpsblktime(int blktime, long flags) {
  return getclient().tpsblktime(blktime, flags);
}
int tpgblktime(long flags) { return getclient().tpgblktime(flags); }

int get_queue(const char *svc) { return getclient().get_queue(svc); }
