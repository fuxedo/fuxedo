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

#include <algorithm>
#include <catch.hpp>
#include <iostream>
#include <stdexcept>

#include "../src/ipc.h"

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

  fux::ipc::qsend(msqid, rq, 0);

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
    try {
      fux::ipc::qsend(msqid, rq, IPC_NOWAIT);
    } catch (...) {
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

  fux::ipc::qsend(msqid, rq, 0);

  fux::ipc::qrecv(msqid, rs, 0, 0);

  REQUIRE(rs.size() == rq.size());
  REQUIRE(rs->mtype == 1);
  REQUIRE(rs->ttype == fux::ipc::file);
  REQUIRE(rs->flags == 1);
  REQUIRE(rs->cd == 2);
}
