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
#include <memory>
#include <vector>

#include "ctxt.h"
#include "ipc.h"
#include "mib.h"
#include "misc.h"
#include "calldesc.h"

struct service_entry {
  std::vector<int> msqids;
  size_t idx;
  size_t service_id;
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
      entry.service_id = m_.find_service(svc);
      if (entry.service_id == m_.badoff) {
        throw std::out_of_range(svc);
      }
      entry.revision = &(m_.services().at(entry.service_id).revision);
      entry.cached_revision = 0;
      it = services_.insert(std::make_pair(strdup(svc), entry)).first;
    }

    auto &entry = it->second;
    if (entry.cached_revision != *(entry.revision)) {
      entry.msqids.clear();
      auto lock = m_.data_lock();
      auto adv = m_.advertisements();
      for (size_t i = 0; i < adv->len; i++) {
        auto &a = adv.at(i);
        if (a.service == entry.service_id) {
          auto msqid = m_.queues().at(a.queue).msqid;
          entry.msqids.push_back(msqid);
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

class client_context {
 public:
  client_context(mib &mibcon) : mibcon_(mibcon), repo_(mibcon) {
    auto lock = mibcon_.data_lock();
    client_ = mibcon_.make_accesser(getpid());
    rpid = mibcon_.accessers().at(client_).rpid = fux::ipc::qcreate();
  }

  ~client_context() {
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
    fux::atmi::reset_tperrno();
    return 0;
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

static thread_local std::shared_ptr<client_context> cctxt;
static client_context &getctxt() {
  if (!cctxt.get()) {
    cctxt = std::make_shared<client_context>(getmib());
  }
  return *cctxt;
}

int tpinit(TPINIT *tpinfo) {
  getctxt();
  fux::atmi::reset_tperrno();
  return 0;
}

int tpterm() {
  cctxt.reset();
  fux::atmi::reset_tperrno();
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
