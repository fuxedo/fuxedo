// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <algorithm>
#include <catch.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>

#include <filesystem>

#include "../../src/ipc.h"
namespace fs = std::filesystem;

static size_t kernel_msgmax(int msqid) {
  struct msginfo info;
  // IPC_INFO answers the highest msqid index, not 0
  REQUIRE(msgctl(msqid, IPC_INFO,
                 reinterpret_cast<struct msqid_ds *>(&info)) >= 0);
  return static_cast<size_t>(info.msgmax);
}

// spill files land in $TMPDIR like the platform; snapshot that dir
static std::string spill_dir() {
  const char *d = getenv("TMPDIR");
  return (d != nullptr && d[0] != '\0') ? d : "/tmp";
}

struct queue_fixture {
  int msqid;
  fux::ipc::msg rq, rs;
  queue_fixture() { msqid = fux::ipc::qcreate(); }
  ~queue_fixture() { msgctl(msqid, IPC_RMID, NULL); }
};

TEST_CASE_METHOD(queue_fixture, "send and receive ipc message", "[ipc]") {
  rq.resize(1024);

  rq->mtype = 1;
  std::copy_n("ServiceName", sizeof("ServiceName"), rq->servicename);
  rq->flags = 1;
  rq->cd = 2;

  fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noflags);

  fux::ipc::qrecv(msqid, rs, 0, 0);

  REQUIRE(rs.size() == rq.size());
  REQUIRE(rs->mtype == 1);
  REQUIRE(rs->ttype == fux::ipc::queue);
  REQUIRE(rs->flags == 1);
  REQUIRE(rs->cd == 2);
}

TEST_CASE_METHOD(queue_fixture, "send multiple and receive by id", "[ipc]") {
  rq.resize(512);

  int i = 1;
  while (true) {
    rq->mtype = i;
    if (!fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock)) {
      i--;
      break;
    }
    i++;
  }

  REQUIRE(i > 2);

  while (i > 0) {
    fux::ipc::qrecv(msqid, rs, i, 0);
    REQUIRE(rs->mtype == i);
    i--;
  }
}

TEST_CASE_METHOD(queue_fixture, "send and receive file message", "[ipc]") {
  rq.resize(10240);
  rq->mtype = 1;
  std::copy_n("ServiceName", sizeof("ServiceName"), rq->servicename);
  rq->flags = 1;
  rq->cd = 2;

  auto snapshot = [] {
    std::vector<std::string> out;
    for (auto &p : fs::directory_iterator(spill_dir())) {
      if (p.path().filename().string().rfind("TUX", 0) == 0) {
        out.push_back(p.path().string());
      }
    }
    return out;
  };
  auto before = snapshot();

  fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noflags);

  // the queue carries only the file name; the payload waits in /tmp/TUX*
  // like the platform's file transfer, and disappears once consumed
  std::vector<std::string> created;
  for (auto &p : fs::directory_iterator(spill_dir())) {
    auto path = p.path().string();
    auto name = p.path().filename().string();
    if (name.rfind("TUX", 0) == 0 &&
        std::find(before.begin(), before.end(), path) == before.end()) {
      created.push_back(path);
    }
  }
  REQUIRE(!created.empty());

  fux::ipc::qrecv(msqid, rs, 0, 0);

  REQUIRE(rs.size() == rq.size());
  REQUIRE(rs->mtype == 1);
  REQUIRE(rs->ttype == fux::ipc::file);
  REQUIRE(rs->flags == 1);
  REQUIRE(rs->cd == 2);
  for (auto &f : created) {
    REQUIRE(!fs::exists(f));
  }
}

TEST_CASE_METHOD(queue_fixture, "kernel-sized message goes direct", "[ipc]") {
  // the platform packs messages right up to the kernel per-message
  // limit; only above it the payload spills into a file
  size_t msgmax = kernel_msgmax(msqid);
  rq.resize(msgmax + sizeof(long));
  rq->mtype = 1;
  std::copy_n("ServiceName", sizeof("ServiceName"), rq->servicename);

  fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noflags);
  fux::ipc::qrecv(msqid, rs, 0, 0);
  REQUIRE(rs->ttype == fux::ipc::queue);
  REQUIRE(rs.size() == rq.size());

  rq.resize(msgmax + sizeof(long) + 1);
  fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noflags);
  fux::ipc::qrecv(msqid, rs, 0, 0);
  REQUIRE(rs->ttype == fux::ipc::file);
  REQUIRE(rs.size() == rq.size());
}

TEST_CASE_METHOD(queue_fixture, "blocking send on full queue spills payload",
                 "[ipc]") {
  rq.resize(512);
  rq->mtype = 1;
  std::copy_n("ServiceName", sizeof("ServiceName"), rq->servicename);

  int queued = 0;
  while (fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock)) {
    queued++;
  }
  REQUIRE(queued > 0);

  // squeeze out the trailing slot: the control message must not fit
  // until space opens, or the spill path completes instantly
  struct msqid_ds ds;
  msgctl(msqid, IPC_STAT, &ds);
  rq.resize(ds.msg_qbytes - ds.__msg_cbytes);
  REQUIRE(fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock));

  // no receiver: the platform reacts to the full queue by spilling the
  // payload immediately, no matter how small, and waiting out the tiny
  // control message instead
  std::thread drainer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    fux::ipc::msg tmp;
    fux::ipc::qrecv(msqid, tmp, 0, 0);
  });
  rq->mtype = 999;
  rq.resize(600);
  auto t0 = std::chrono::steady_clock::now();
  REQUIRE(fux::ipc::qsend(msqid, rq, 5000, fux::ipc::flags::noflags));
  auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
  drainer.join();
  REQUIRE(waited >= 250);

  fux::ipc::qrecv(msqid, rs, 999, 0);
  REQUIRE(rs->ttype == fux::ipc::file);
  REQUIRE(rs.size() == rq.size());
  REQUIRE(std::memcmp(rs.buf() + sizeof(fux::ipc::msgbase),
                      rq.buf() + sizeof(fux::ipc::msgbase),
                      rq.size() - sizeof(fux::ipc::msgbase)) == 0);

  msgctl(msqid, IPC_STAT, &ds);
  while (ds.msg_qnum-- > 0) {
    fux::ipc::msg tmp;
    fux::ipc::qrecv(msqid, tmp, 0, 0);
  }
}

TEST_CASE_METHOD(queue_fixture, "small blocking send retries inline on full queue",
                 "[ipc]") {
  // the platform only spills on the blocking full-queue path when the
  // whole IPC message clears 500 bytes; smaller messages retry inline
  rq.resize(400);
  rq->mtype = 1;
  std::copy_n("ServiceName", sizeof("ServiceName"), rq->servicename);

  int queued = 0;
  while (fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock)) {
    queued++;
  }
  REQUIRE(queued > 0);
  struct msqid_ds ds;
  msgctl(msqid, IPC_STAT, &ds);
  rq.resize(ds.msg_qbytes - ds.__msg_cbytes);
  REQUIRE(fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock));

  std::thread drainer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    fux::ipc::msg tmp;
    fux::ipc::qrecv(msqid, tmp, 0, 0);
  });
  rq->mtype = 999;
  rq.resize(400);
  REQUIRE(fux::ipc::qsend(msqid, rq, 5000, fux::ipc::flags::noflags));
  drainer.join();

  fux::ipc::qrecv(msqid, rs, 999, 0);
  REQUIRE(rs->ttype == fux::ipc::queue);
  REQUIRE(rs.size() == rq.size());

  msgctl(msqid, IPC_STAT, &ds);
  while (ds.msg_qnum-- > 0) {
    fux::ipc::msg tmp;
    fux::ipc::qrecv(msqid, tmp, 0, 0);
  }
}

TEST_CASE_METHOD(queue_fixture, "noblock send on full queue leaves no spill",
                 "[ipc]") {
  auto snapshot = [] {
    std::vector<std::string> out;
    for (auto &p : fs::directory_iterator(spill_dir())) {
      if (p.path().filename().string().rfind("TUX", 0) == 0) {
        out.push_back(p.path().string());
      }
    }
    return out;
  };
  auto before = snapshot();

  rq.resize(512);
  int queued = 0;
  while (fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock)) {
    queued++;
  }
  REQUIRE(queued > 0);
  struct msqid_ds ds;
  msgctl(msqid, IPC_STAT, &ds);
  rq.resize(ds.msg_qbytes - ds.__msg_cbytes);
  REQUIRE(fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock));

  // oversize + NOBLOCK + full queue: fail clean, unlink the spill file
  rq.resize(16384);
  REQUIRE(!fux::ipc::qsend(msqid, rq, 0, fux::ipc::flags::noblock));

  auto after = snapshot();
  REQUIRE(after.size() == before.size());

  msgctl(msqid, IPC_STAT, &ds);
  while (ds.msg_qnum-- > 0) {
    fux::ipc::msg tmp;
    fux::ipc::qrecv(msqid, tmp, 0, 0);
  }
}
