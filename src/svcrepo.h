// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <map>
#include "mib.h"
#include "misc.h"

struct queue_entry {
  int grpno;
  int msqid;
};

struct service_entry {
  std::vector<queue_entry> queues;
  size_t current_queue;
  uint64_t cached_revision;
  uint64_t *mib_revision;
  size_t mib_service;

  service_entry() : current_queue(0) {}
};

class service_repository {
 public:
  service_repository(mib &m) : m_(m) {}
  int get_queue(int grpno, const char *svc) {
    auto &entry = get_entry(svc);
    refresh(entry);
    return load_balance(entry, grpno);
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
          auto grpno = m_.servers().at(a.server).grpno;
          entry.queues.push_back({grpno, msqid});
        }
      }
      entry.cached_revision = *(entry.mib_revision);
    }
  }

  int load_balance(service_entry &entry, int grpno) {
    if (entry.queues.empty()) {
      throw std::out_of_range("no queue");
    }
    if (grpno == -1) {
      entry.current_queue = (entry.current_queue + 1) % entry.queues.size();
      return entry.queues[entry.current_queue].msqid;
    } else {
      for (auto &e : entry.queues) {
        if (e.grpno == grpno) {
          return e.msqid;
        }
      }
    }
    throw std::out_of_range("no queue");
  }

  mib &m_;
  std::map<std::string, service_entry, std::less<void>> services_;
};
