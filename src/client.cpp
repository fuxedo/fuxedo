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

#include <xa.h>
#include <xatmi.h>

#include <algorithm>
#include <clara.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#include "ipc.h"
#include "mib.h"
#include "misc.h"

struct service_entry {
  std::vector<int> msqids;
  size_t idx;
  size_t service;
  uint64_t cached_revision;
  uint64_t *revision;

  service_entry() : idx(0) {}
};

class service_repository {
 public:
  service_repository(mib &m) : m_(m) {}
  int get_queue(const char *svc) {
    auto it = services_.find(svc);
    if (it == services_.end()) {
      auto lock = m_.data_lock();
      service_entry entry;
      entry.service = m_.find_service(svc);
      if (entry.service == m_.badoff) {
        throw std::out_of_range(svc);
      }
      entry.revision = &(m_.services().at(entry.service).revision);
      entry.cached_revision = 0;
      it = services_.insert(std::make_pair(strdup(svc), entry)).first;
    }

    auto &entry = it->second;
    if (entry.cached_revision != *(entry.revision)) {
      auto lock = m_.data_lock();
      auto adv = m_.advertisements();
      for (size_t i = 0; i < adv.len(); i++) {
        auto &a = adv.at(i);
        if (a.service == entry.service) {
          auto msqid = m_.queues().at(a.queue).msqid;
          if (std::find(std::begin(entry.msqids), std::end(entry.msqids),
                        msqid) == std::end(entry.msqids)) {
            entry.msqids.push_back(msqid);
          }
        }
      }
      entry.cached_revision = *(entry.revision);
    }

    if (entry.msqids.empty()) {
      throw std::out_of_range(svc);
    }
    entry.idx = (entry.idx + 1) % entry.msqids.size();
    return entry.msqids[entry.idx];
  }

 private:
  mib &m_;
  std::map<const char *, service_entry, cmp_cstr> services_;
};

class call_describtors {
 private:
  std::vector<int> cds_;
  int seq_;

  // cycle 1..x
  int nextseq() {
    seq_++;
    if (seq_ > 0xffffff) {
      seq_ = 1;
    }
    return seq_;
  }

  void insert_cd(int cd) {
    cds_.insert(std::upper_bound(std::begin(cds_), std::end(cds_), cd), cd);
  }

  auto find_cd(int cd) {
    return std::upper_bound(std::begin(cds_), std::end(cds_), cd);
  }

 public:
  call_describtors() : seq_(0) {}

  int allocate() {
    if (cds_.size() > 128) {
      TPERROR(TPELIMIT, "No free call descriptors");
      return -1;
    }
    while (true) {
      auto cd = nextseq();
      if (find_cd(cd) == cds_.end()) {
        insert_cd(cd);
        return cd;
      }
    }
  }

  int release(int cd) {
    auto it = find_cd(cd);
    if (it == cds_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    cds_.erase(it);
    return 0;
  }
};

class client_context {
 public:
  client_context(mib &mibcon) : mibcon_(mibcon), repo_(mibcon) {
    auto lock = mibcon_.data_lock();
    client_ = mibcon_.make_client(getpid());
    rpid = mibcon_.clients().at(client_).rpid = fux::ipc::qcreate();
  }

  ~client_context() {
    auto lock = mibcon_.data_lock();
    fux::ipc::qdelete(mibcon_.clients().at(client_).rpid);
    mibcon_.clients().at(client_).rpid = -1;
  }

  int tpacall(const char *svc, char *data, long len, long flags) {
    int msqid = repo_.get_queue(svc);
    int cd = cds_.allocate();
    // TPERROR(TPELIMIT, "No free call descriptors");
    // return -1;

    rq.set_data(data, len);

    checked_copy(svc, rq->servicename);
    rq->mtype = cd;
    rq->cd = cd;
    rq->replyq = rpid;

    fux::ipc::qsend(msqid, rq, 0);
    return cd;
  }

  int tpgetrply(int *cd, char **data, long *len, long flags) {
    fux::ipc::qrecv(rpid, res, *cd, 0);
    res.get_data(data);
    tpurcode = res->rcode;
    if (len != nullptr) {
      *len = res.size_data();
    }
    return 0;
  }

  int tpcancel(int cd) {
    if (cds_.release(cd) == -1) {
      return -1;
    }
    // TPETRAN
    return 0;
  }

 private:
  mib &mibcon_;
  size_t client_;
  call_describtors cds_;
  service_repository repo_;
  fux::ipc::msg rq, res;
  int rpid;
};

static thread_local std::unique_ptr<client_context> tls_ctxt;
static client_context &getctxt() {
  if (!tls_ctxt.get()) {
    tls_ctxt = std::make_unique<client_context>(getmib());
  }
  return *tls_ctxt;
}

int tpinit(TPINIT *tpinfo) {
  getctxt();
  return 0;
}

int tpterm() {
  tls_ctxt.reset();
  return 0;
}

int tpcancel(int cd) { return getctxt().tpcancel(cd); }

int tpacall(char *svc, char *data, long len, long flags) {
  return getctxt().tpacall(svc, data, len, flags);
}

int tpgetrply(int *cd, char **data, long *len, long flags) {
  return getctxt().tpgetrply(cd, data, len, flags);
}
int tpcall(char *svc, char *idata, long ilen, char **odata, long *olen,
           long flags) {
  int cd = tpacall(svc, idata, ilen, flags);
  if (cd == -1) {
    return -1;
  }
  return tpgetrply(&cd, odata, olen, flags);
}
