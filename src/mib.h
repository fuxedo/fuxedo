#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

#include <sys/ipc.h>
#include <sys/shm.h>

#include "ipc.h"
#include "misc.h"
#include "ubb.h"

tuxconfig getconfig(ubbconfig *ubb = nullptr);

struct transaction_table;

constexpr auto INVALID_TIME =
    std::chrono::time_point<std::chrono::steady_clock>::max();

struct accesser {
  pid_t pid;
  int rpid;
  std::chrono::steady_clock::time_point rpid_timeout;

  bool valid() const { return pid != 0; }
  void invalidate() {
    pid = 0;
    rpid = -1;
  }
};

struct machine {
  // char address[30];
  // Does not fit Travis CI machine names like
  // travis-job-639f543d-4c99-47ee-b050-1a08446902d8
  char address[64];
  char lmid[30];
  char tuxconfig[256];
  char tuxdir[256];
  char appdir[256];
  char ulogpfx[256];
  char tlogdevice[256];
  long blocktime;
};

struct group {
  uint16_t grpno;
  char srvgrp[32];
  char openinfo[256];
  char closeinfo[256];
  char tmsname[256];
};

enum class state_t : uint8_t {
  undefined = 0,
  MIGrating,
  CLEaning,
  REStarting,
  SUSpended,
  PARtitioned,
  DEAd,
  NEW,
  INValid,
  ACTive,
  INActive
};

struct server {
  uint16_t srvid;
  uint16_t grpno;
  bool autostart;
  pid_t pid;
  time_t last_alive_time;
  size_t rqaddr;
  state_t state;
  char servername[128];
  char clopt[1024];

  void suspend() { state = state_t::SUSpended; }

  bool is_inactive() const { return state == state_t::INActive; }
};

struct queue {
  char rqaddr[32];
  int msqid;
  long mtype;
};

struct service {
  char servicename[XATMI_SERVICE_NAME_LENGTH];
  uint64_t revision;
  void modified() { revision++; }
};

struct advertisement {
  size_t service;
  size_t queue;
  size_t server;
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

struct mibmem {
  std::atomic<int> state;
  int mainsem;

  uint32_t host;
  uint32_t counter __attribute__((aligned(64)));

  tuxconfig conf;
  machine mach;

  mibarr<group> groups;
  mibarr<server> servers;
  mibarr<queue> queues;
  mibarr<service> services;
  mibarr<advertisement> advertisements;
  mibarr<accesser> accessers;

  size_t transactions_off;
  char data[];
};

namespace fux {
namespace mib {
struct in_heap {};
}  // namespace mib
}  // namespace fux

template <typename T>
class mib_iterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = int;
  using pointer = T *;
  using reference = T &;
};
/*
class iterator
        {
            public:
                typedef iterator self_type;
                typedef T value_type;
                typedef T& reference;
                typedef T* pointer;
                typedef std::forward_iterator_tag iterator_category;
                typedef int difference_type;
                iterator(pointer ptr) : ptr_(ptr) { }
                self_type operator++() { self_type i = *this; ptr_++; return i;
} self_type operator++(int junk) { ptr_++; return *this; } reference operator*()
{ return *ptr_; } pointer operator->() { return ptr_; } bool operator==(const
self_type& rhs) { return ptr_ == rhs.ptr_; } bool operator!=(const self_type&
rhs) { return ptr_ != rhs.ptr_; } private: pointer ptr_;
        };
*/

class mib {
 public:
  mib(const tuxconfig &cfg);
  mib(const tuxconfig &cfg, fux::mib::in_heap);

  static size_t needed(const tuxconfig &cfg);

  void validate();
  void remove();

  mibmem *operator->() { return mem_; }

  uint64_t genuid();

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
  transaction_table &transactions() {
    return *reinterpret_cast<transaction_table *>(mem_->data +
                                                  mem_->transactions_off);
  }

  void advertise(const std::string &servicename, size_t queue, size_t server);
  void unadvertise(const std::string &servicename, size_t queue, size_t server);

  size_t find_queue(const std::string &rqaddr);
  size_t make_queue(const std::string &rqaddr);

  size_t find_group(uint16_t grpno);
  size_t make_group(uint16_t grpno, const std::string &srvgrp,
                    const std::string &openinfo, const std::string &closeinfo,
                    const std::string &tmsname);
  group &group_by_id(uint16_t grpno) {
    auto idx = find_group(grpno);
    if (idx == badoff) {
      throw std::logic_error("Group not found");
    }
    return groups().at(idx);
  }

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

  void collect(std::vector<int> &q, std::vector<int> &m, std::vector<int> &s);
  void remove(std::vector<int> &q, std::vector<int> &m, std::vector<int> &s);

 private:
  size_t find_advertisement(size_t service, size_t queue, size_t server);
  void init_memory();

  tuxconfig cfg_;
  int shmid_;
  mibmem *mem_;
};

mib &getmib();
std::string getubb();
namespace fux::tx {
extern uint16_t grpno;
}
