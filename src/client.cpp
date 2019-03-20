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
  client(mib &mibcon) : mibcon_(mibcon), repo_(mibcon) {
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

    auto lock = fux::glob::calldescs()->lock();
    int cd = fux::glob::calldescs()->allocate();
    if (cd == -1) {
      return -1;
    }

    rq.set_data(data, len);

    checked_copy(svc, rq->servicename);
    rq->mtype = cd;
    rq->cd = cd;
    if (flags & TPNOREPLY) {
      rq->replyq = -1;
    } else {
      rq->replyq = rpid;
    }

    fux::ipc::qsend(msqid, rq, 0);
    fux::atmi::reset_tperrno();
    return cd;
  } catch (const std::out_of_range &e) {
    TPERROR(TPENOENT, "Service %s does not exist", svc);
    return -1;
  }

  int tpgetrply(int *cd, char **data, long *len, long flags) {
    fux::ipc::qrecv(rpid, res, *cd, 0);
    res.get_data(data);
    tpurcode = res->rcode;
    if (len != nullptr) {
      *len = res.size_data();
    }
    if (res->rval == TPSUCCESS) {
      fux::atmi::reset_tperrno();
      return 0;
    }
    TPERROR(TPESVCFAIL, "Service failed with %d", res->rval);
    return -1;
  }

  int tpcancel(int cd) {
    auto lock = fux::glob::calldescs()->lock();
    if (fux::glob::calldescs()->release(cd) == -1) {
      return -1;
    }
    // TPETRAN
    fux::atmi::reset_tperrno();
    return 0;
  }

 private:
  mib &mibcon_;
  size_t client_;
  service_repository repo_;
  fux::ipc::msg rq, res;
  int rpid;
};

static thread_local std::shared_ptr<client> current_client;
static client &getclient() {
  if (!current_client.get()) {
    current_client = std::make_shared<client>(getmib());
  }
  return *current_client;
}

int tpinit(TPINIT *tpinfo) {
  getclient();
  fux::atmi::reset_tperrno();
  return 0;
}

int tpterm() {
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
