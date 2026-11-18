// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <algorithm>
#include <catch.hpp>
#include <iostream>
#include <stdexcept>

#include <filesystem>

#include "../../src/ipc.h"
namespace fs = std::filesystem;

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
    for (auto &p : fs::directory_iterator("/tmp")) {
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
  for (auto &p : fs::directory_iterator("/tmp")) {
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
