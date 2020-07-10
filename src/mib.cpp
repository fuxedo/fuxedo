// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <time.h>

#include <algorithm>
#include <iterator>
#include <sstream>

#include "mib.h"
#include "ubbreader.h"

#include "misc.h"
#include "trx.h"

#ifdef HAVE_CXX_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

void mib::validate() {
  if (strlen(mach().tlogdevice) > 0) {
    if (!fs::is_directory(mach().tlogdevice)) {
      throw std::invalid_argument(string_format(
          "TLOGDEVICE=%s must be a writable directory", mach().tlogdevice));
    }
  }
}

tuxconfig getconfig(ubbconfig *ubb) {
  auto outfile = fux::util::getenv("TUXCONFIG", "");
  if (outfile.empty()) {
    throw std::runtime_error("TUXCONFIG environment variable required");
  }

  std::ifstream fin;
  fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fin.open(outfile);

  tuxconfig tuxcfg;
  fin.read(reinterpret_cast<char *>(&tuxcfg), sizeof(tuxcfg));

  auto size = fs::file_size(outfile);
  if (tuxcfg.size != size) {
    throw std::runtime_error("Bad TUXCONFIG signature, invalid file");
  }

  if (ubb != nullptr) {
    ubbreader p(fin);
    *ubb = p.parse();
  }

  return tuxcfg;
}

std::string getubb() {
  auto outfile = fux::util::getenv("TUXCONFIG", "");
  if (outfile.empty()) {
    throw std::runtime_error("TUXCONFIG environment variable required");
  }

  std::ifstream fin;
  fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fin.open(outfile);

  tuxconfig tuxcfg;
  fin.read(reinterpret_cast<char *>(&tuxcfg), sizeof(tuxcfg));

  auto size = fs::file_size(outfile);
  if (tuxcfg.size != size) {
    throw std::runtime_error("Bad TUXCONFIG signature, invalid file");
  }

  std::ostringstream sout;
  std::copy(std::istreambuf_iterator<char>(fin),
            std::istreambuf_iterator<char>(),
            std::ostreambuf_iterator<char>(sout));
  return sout.str();
}

template <class T>
size_t init(T &t, size_t size, size_t off) {
  t.len = 0;
  t.size = size;
  t.off = off;
  return nearest64(t.size * sizeof(typename T::value_type));
}

size_t mib::needed(const tuxconfig &cfg) {
  return nearest64(sizeof(mibmem)) +
         nearest64(cfg.maxservers * sizeof(server)) +
         nearest64(cfg.maxqueues * sizeof(queue)) +
         nearest64(cfg.maxservices * sizeof(service)) +
         nearest64(cfg.maxservers * cfg.maxservices * sizeof(advertisement)) +
         nearest64(cfg.maxgroups * sizeof(group)) +
         nearest64(cfg.maxaccessers * sizeof(accesser)) +
         nearest64(transaction_table::needed(100, cfg.maxgroups));
}

void mib::init_memory() {
  auto off = nearest64(sizeof(mibmem));
  off += init(mem_->servers, cfg_.maxservers, off - offsetof(mibmem, servers));
  off += init(mem_->queues, cfg_.maxqueues, off - offsetof(mibmem, queues));
  off +=
      init(mem_->services, cfg_.maxservices, off - offsetof(mibmem, services));
  off += init(mem_->groups, cfg_.maxgroups, off - offsetof(mibmem, groups));
  off += init(mem_->advertisements, cfg_.maxservers * cfg_.maxservices,
              off - offsetof(mibmem, advertisements));
  off += init(mem_->accessers, cfg_.maxaccessers,
              off - offsetof(mibmem, accessers));

  mem_->transactions_off = off;
  transactions().init(100, cfg_.maxgroups);

  off += nearest64(transaction_table::needed(transactions().size,
                                             transactions().max_groups));
}

size_t mib::find_advertisement(size_t service, size_t queue, size_t server) {
  for (size_t i = 0; i < advertisements()->len; i++) {
    auto &advertisement = advertisements().at(i);
    if (advertisement.service == service && advertisement.queue == queue &&
        advertisement.server == server) {
      return i;
    }
  }
  return badoff;
}

void mib::advertise(const std::string &servicename, size_t queue,
                    size_t server) {
  auto service = find_service(servicename);
  if (service == badoff) {
    service = make_service(servicename);
  }

  if (find_advertisement(service, queue, server) != badoff) {
    throw std::logic_error("Already advertised");
  }

  auto i = find_advertisement(badoff, badoff, badoff);
  if (i == badoff) {
    i = advertisements()->len;
  }

  auto &advertisement = advertisements().at(i);

  advertisement.service = service;
  advertisement.queue = queue;
  advertisement.server = server;

  if (i == advertisements()->len) {
    advertisements()->len++;
  }
  services().at(service).modified();
}

void mib::unadvertise(const std::string &servicename, size_t queue,
                      size_t server) {
  auto service = find_service(servicename);
  if (service == badoff) {
    throw std::logic_error("Unknown service");
  }

  auto i = find_advertisement(service, queue, server);
  if (i == badoff) {
    throw std::logic_error("Service not advertised");
  }
  auto &advertisement = advertisements().at(i);
  advertisement.service = advertisement.queue = advertisement.server = badoff;
  services().at(service).modified();
}

size_t mib::find_queue(const std::string &rqaddr) {
  for (size_t i = 0; i < queues()->len; i++) {
    if (rqaddr == queues().at(i).rqaddr) {
      return i;
    }
  }
  return badoff;
}

size_t mib::make_queue(const std::string &rqaddr) {
  if (find_queue(rqaddr) != badoff) {
    throw std::logic_error("Duplicate queue");
  }

  auto &queue = queues().at(queues()->len);
  checked_copy(rqaddr, queue.rqaddr);
  queue.msqid = -1;
  queue.mtype = std::numeric_limits<long>::max();

  return queues()->len++;
}

size_t mib::find_group(uint16_t grpno) {
  for (size_t i = 0; i < groups()->len; i++) {
    auto &group = groups().at(i);
    if (group.grpno == grpno) {
      return i;
    }
  }
  return badoff;
}

size_t mib::make_group(uint16_t grpno, const std::string &srvgrp,
                       const std::string &openinfo,
                       const std::string &closeinfo,
                       const std::string &tmsname) {
  if (find_group(grpno) != badoff) {
    throw std::logic_error("Duplicate group");
  }

  auto &group = groups().at(groups()->len);
  group.grpno = grpno;
  checked_copy(srvgrp, group.srvgrp);
  checked_copy(openinfo, group.openinfo);
  checked_copy(closeinfo, group.closeinfo);
  checked_copy(tmsname, group.tmsname);

  return groups()->len++;
}

size_t mib::find_server(uint16_t srvid, uint16_t grpno) {
  for (size_t i = 0; i < servers()->len; i++) {
    auto &server = servers().at(i);
    if (server.srvid == srvid && server.grpno == grpno) {
      return i;
    }
  }
  return badoff;
}

size_t mib::make_server(uint16_t srvid, uint16_t grpno,
                        const std::string &servername, const std::string &clopt,
                        const std::string &rqaddr) {
  if (find_server(srvid, grpno) != badoff) {
    throw std::logic_error("Duplicate server");
  }

  auto &server = servers().at(servers()->len);
  server.srvid = srvid;
  server.grpno = grpno;
  checked_copy(servername, server.servername);
  checked_copy(clopt, server.clopt);

  server.rqaddr = find_queue(rqaddr);
  if (server.rqaddr == badoff) {
    server.rqaddr = make_queue(rqaddr);
  }
  return servers()->len++;
}

size_t mib::find_service(const char *servicename) {
  for (size_t i = 0; i < services()->len; i++) {
    if (strcmp(servicename, services().at(i).servicename) == 0) {
      return i;
    }
  }
  return badoff;
}
size_t mib::find_service(const std::string &servicename) {
  return find_service(servicename.c_str());
}

size_t mib::make_accesser(pid_t pid) {
  ssize_t at = -1;
  for (size_t i = 0; i < accessers()->len; i++) {
    if (accessers().at(i).pid == 0) {
      at = i;
      break;
    }
  }
  if (at == -1) {
    at = accessers()->len++;
  }
  auto &accesser = accessers().at(at);
  accesser.rpid_timeout = INVALID_TIME;
  accesser.pid = pid;
  return at;
}

size_t mib::make_service(const std::string &servicename) {
  if (find_service(servicename) != badoff) {
    throw std::logic_error("Duplicate service");
  }

  auto &service = services().at(services()->len);
  checked_copy(servicename, service.servicename);
  return services()->len++;
}

int mib::make_service_rqaddr(size_t server) {
  auto &queue = queues().at(servers().at(server).rqaddr);
  if (queue.msqid == -1) {
    queue.msqid = fux::ipc::qcreate();
    if (queue.msqid == -1) {
      throw std::system_error(errno, std::system_category());
    }
  }
  return queue.msqid;
}

void mib::remove() {
  std::vector<int> q, m, s;
  collect(q, m, s);
  remove(q, m, s);
}

void mib::collect(std::vector<int> &q, std::vector<int> &m,
                  std::vector<int> &s) {
  for (size_t i = 0; i < queues()->len; i++) {
    auto msqid = queues().at(i).msqid;
    if (msqid != -1) {
      q.push_back(msqid);
    }
  }
  for (size_t i = 0; i < accessers()->len; i++) {
    auto &acc = accessers().at(i);
    if (acc.rpid != -1) {
      q.push_back(acc.rpid);
    }
  }
  s.push_back(mem_->mainsem);
  m.push_back(shmid_);
}

void mib::remove(std::vector<int> &q, std::vector<int> &m,
                 std::vector<int> &s) {
  for (int i : q) {
    msgctl(i, IPC_RMID, NULL);
  }
  for (int i : s) {
    fux::ipc::semrm(i);
  }
  for (int i : m) {
    if (shmctl(i, IPC_RMID, NULL) == -1) {
      throw std::system_error(errno, std::system_category(),
                              "shmctl(IPC_RMID) failed");
    }
  }
}

void ubb2mib(ubbconfig &u, mib &m);

mib &getmib() {
  static std::unique_ptr<mib> mibcon;

  if (!mibcon.get()) {
    auto cfg = getconfig();
    mibcon = std::make_unique<mib>(cfg);
    if (mibcon->servers()->len == 0) {
      ubbconfig u;
      getconfig(&u);
      ubb2mib(u, *mibcon.get());
    }
  }

  return *mibcon.get();
}

mib::mib(const tuxconfig &cfg) : cfg_(cfg) {
  shmid_ = shmget(cfg_.ipckey, needed(cfg_), 0600 | IPC_CREAT);
  if (shmid_ == -1) {
    throw std::system_error(errno, std::system_category(), "shmget failed");
  }

  mem_ = reinterpret_cast<mibmem *>(shmat(shmid_, nullptr, 0));
  if (mem_ == reinterpret_cast<void *>(-1)) {
    throw std::system_error(errno, std::system_category(), "shmat failed");
  }

  int z = 0;
  if (mem_->state.compare_exchange_strong(z, 1)) {
    mem_->mainsem = fux::ipc::seminit(IPC_PRIVATE, 1);
    mem_->conf = cfg_;
    init_memory();
    mem_->state = 2;
  }

  while (mem_->state != 2) {
    std::this_thread::yield();
  }
}

mib::mib(const tuxconfig &cfg, fux::mib::in_heap) : cfg_(cfg) {
  mem_ = reinterpret_cast<mibmem *>(calloc(1, needed(cfg_)));
  init_memory();
}

uint64_t mib::genuid() {
  struct timespec ts;
  (void)clock_gettime(CLOCK_MONOTONIC, &ts);

  uint64_t gtrid = 0;
  do {
    auto counter = __sync_fetch_and_add(&(mem_->counter), 1);

    // 10 bits machine identifier (from UBBCONFIG), 1024 machines
    // 30 bits value representing the seconds since something, 30+years
    // 24 bits counter, starting with a random value, 1,073,741,824 per second
    gtrid = (uint64_t(mem_->host & 0b1111111111) << 54) +
            (uint64_t(ts.tv_sec & 0b111111111111111111111111111111) << 24) +
            (uint64_t(counter & 0b111111111111111111111111));

    // highly unlikely but 0 looks too bad
  } while (gtrid == 0);
  return gtrid;
}
