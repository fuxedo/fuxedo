#pragma once
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

#include <atomic>
#include <cstdint>
#include <limits>
#include <thread>

#include <sys/ipc.h>
#include <sys/shm.h>

#include "ipc.h"
#include "ubb.h"

size_t filesize(const std::string &filename);
tuxconfig getconfig(ubbconfig *ubb = nullptr);

struct accesser {
  int sem;
  pid_t pid;
  int rpid;
};

struct machine {
  char address[30];
  char lmid[30];
  char tuxconfig[256];
  char tuxdir[256];
  char appdir[256];
  char ulogpfx[256];
  char tlogdevice[256];
};

struct group {
  uint16_t grpno;
  char srvgrp[32];
  char openinfo[256];
  char tmsname[256];
};

struct server {
  uint16_t srvid;
  uint16_t grpno;
  bool autostart;
  pid_t pid;
  time_t last_alive_time;
  size_t rqaddr;
  char servername[128];
  char clopt[1024];
};

struct queue {
  char rqaddr[32];
  int msqid;
  long mtype;
};

struct service {
  char servicename[XATMI_SERVICE_NAME_LENGTH];
  uint64_t revision;
};

struct advertisement {
  size_t service;
  size_t queue;
};

template <typename T>
struct mibarr {
  size_t len;
  size_t size;
  uint64_t revision;
  size_t off;
  typedef T value_type;
};

template <typename T>
class mibarrptr {
 public:
  mibarrptr(mibarr<T> *p) : p_(p) {}

  mibarr<T> *operator->() { return p_; }
  T &operator[](size_t pos) const {
    return reinterpret_cast<T *>(reinterpret_cast<char *>(p_) + p_->off)[pos];
  }
  T &at(size_t pos) const {
    if (!(pos < p_->size)) {
      throw std::out_of_range("at");
    }
    return (*this)[pos];
  }

  auto size() const { return p_->size; }

 private:
  mibarr<T> *p_;
};

static constexpr size_t nearest64(size_t n, size_t mult = 1) {
  return n <= (mult * 64) ? (mult * 64) : nearest64(n, mult + 1);
}

struct mibmem {
  std::atomic<int> state;
  int mainsem;

  tuxconfig conf;
  machine mach;

  mibarr<group> groups;
  mibarr<server> servers;
  mibarr<queue> queues;
  mibarr<service> services;
  mibarr<advertisement> advertisements;
  mibarr<accesser> accessers;

  char data[];
};

namespace fux {
namespace mib {
struct in_heap {};
}
}

class mib {
 public:
  mib(const tuxconfig &cfg);
  mib(const tuxconfig &cfg, fux::mib::in_heap);

  static size_t needed(const tuxconfig &cfg);

  void remove();

  auto &conf() { return mem_->conf; }
  auto &mach() { return mem_->mach; }
  auto groups() { return mibarrptr<group>(&mem_->groups); }
  auto servers() { return mibarrptr<server>(&mem_->servers); }
  auto queues() { return mibarrptr<queue>(&mem_->queues); }
  auto services() { return mibarrptr<service>(&mem_->services); }
  auto accessers() { return mibarrptr<accesser>(&mem_->accessers); }
  auto advertisements() {
    return mibarrptr<advertisement>(&mem_->advertisements);
  }

  void advertise(const std::string &servicename, size_t queue);
  void unadvertise(const std::string &servicename, size_t queue);

  size_t find_queue(const std::string &rqaddr);
  size_t make_queue(const std::string &rqaddr);

  size_t find_server(uint16_t srvid, uint16_t grpno);
  size_t make_server(uint16_t srvid, uint16_t grpno,
                     const std::string &servername, const std::string &clopt,
                     const std::string &rqaddr);

  size_t find_service(const std::string &servicename);
  size_t find_service(const char *servicename);
  size_t make_service(const std::string &servicename);

  size_t make_accesser(pid_t pid);

  int make_service_rqaddr(size_t server);

  fux::ipc::scoped_semlock data_lock() {
    return fux::ipc::scoped_semlock(mem_->mainsem, 0);
  }

  constexpr static size_t badoff = std::numeric_limits<size_t>::max();

 private:
  size_t find_advertisement(size_t service, size_t queue);
  void init_memory();

  tuxconfig cfg_;
  int shmid_;
  mibmem *mem_;
};

mib &getmib();
std::string getubb();
