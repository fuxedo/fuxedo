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

#include <clara.hpp>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <algorithm>
#include <iterator>
#include <sstream>

#include "mib.h"
#include "ubbreader.h"

#include "misc.h"

size_t filesize(const std::string &filename) {
  std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
  return in.tellg();
}

tuxconfig getconfig(ubbconfig *ubb) {
  auto outfile = std::getenv("TUXCONFIG");
  if (outfile == nullptr) {
    throw std::runtime_error("TUXCONFIG environment variable required");
  }

  std::ifstream fin;
  fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fin.open(outfile);

  tuxconfig tuxcfg;
  fin.read(reinterpret_cast<char *>(&tuxcfg), sizeof(tuxcfg));

  auto size = filesize(outfile);
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
  auto outfile = std::getenv("TUXCONFIG");
  if (outfile == nullptr) {
    throw std::runtime_error("TUXCONFIG environment variable required");
  }

  std::ifstream fin;
  fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fin.open(outfile);

  tuxconfig tuxcfg;
  fin.read(reinterpret_cast<char *>(&tuxcfg), sizeof(tuxcfg));

  auto size = filesize(outfile);
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
         nearest64(cfg.maxaccessers * sizeof(accesser));
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
}

size_t mib::find_advertisement(size_t service, size_t queue) {
  for (size_t i = 0; i < advertisements()->len; i++) {
    auto &advertisement = advertisements().at(i);
    if (advertisement.service == service && advertisement.queue == queue) {
      return i;
    }
  }
  return badoff;
}

void mib::advertise(const std::string &servicename, size_t queue) {
  auto service = find_service(servicename);
  if (service == badoff) {
    service = make_service(servicename);
  }
  if (find_advertisement(service, queue) == badoff) {
    auto &advertisement = advertisements().at(advertisements()->len);
    advertisement.service = service;
    advertisement.queue = queue;
    services().at(service).revision++;
    advertisements()->len++;
  }
}

void mib::unadvertise(const std::string &servicename, size_t queue) {
  auto service = find_service(servicename);
  if (service == badoff) {
    throw std::logic_error("Unknown service");
  }
  auto advertisement = find_advertisement(service, queue);
  if (advertisement != badoff) {
  }
  throw std::logic_error("Service not advertised");
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
  return queues()->len++;
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
  for (size_t i = 0; i < accessers()->len; i++) {
    if (accessers().at(i).pid == 0) {
      accessers().at(i).sem = fux::ipc::seminit(IPC_PRIVATE, 1);
      accessers().at(i).pid = pid;
      return i;
    }
  }
  auto &accesser = accessers().at(accessers()->len);
  accesser.pid = pid;
  return accessers()->len++;
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
  std::cout << queue.rqaddr << "  " << queue.msqid << std::endl;
  if (queue.msqid == -1) {
    queue.msqid = fux::ipc::qcreate();
    if (queue.msqid == -1) {
      throw std::system_error(errno, std::system_category());
    }
  }
  return queue.msqid;
}

void mib::remove() {
  for (size_t i = 0; i < queues()->len; i++) {
    auto msqid = queues().at(i).msqid;
    if (msqid != -1) {
      msgctl(msqid, IPC_RMID, NULL);
      queues().at(i).msqid = -1;
    }
  }
  for (size_t i = 0; i < accessers()->len; i++) {
    auto &acc = accessers().at(i);
    if (acc.sem != -1) {
      fux::ipc::semrm(acc.sem);
      acc.sem = -1;
    }
    if (acc.rpid != -1) {
      msgctl(acc.rpid, IPC_RMID, NULL);
      acc.rpid = -1;
    }
  }
  fux::ipc::semrm(mem_->mainsem);
  shmctl(shmid_, IPC_RMID, NULL);
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
  }

  mem_ = reinterpret_cast<mibmem *>(shmat(shmid_, nullptr, 0));

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
