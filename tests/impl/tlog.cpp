// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

#include <string>

#include <catch.hpp>

#include "tlog.h"

namespace {

std::string device(const char *name) {
  auto dir = std::string("tlog-") + name + "-" + std::to_string(getpid());
  std::string cmd = "rm -rf " + dir;
  if (system(cmd.c_str()) != 0) {
  }
  return dir;
}

XID mkxid(const char *gtrid, int bqual = 1) {
  XID xid = {};
  xid.formatID = 42;
  xid.gtrid_length = strlen(gtrid);
  xid.bqual_length = sizeof(int);
  memcpy(xid.data, gtrid, xid.gtrid_length);
  memcpy(xid.data + xid.gtrid_length, &bqual, sizeof(int));
  return xid;
}

}  // namespace

TEST_CASE("create and open a tlog device") {
  auto dir = device("create");
  fux::tlog::log::create(dir, 7, 3);
  auto log = fux::tlog::log::open(dir);
  CHECK(log.offset() == 7);
  CHECK(log.size() == 3);
  CHECK(log.recent().empty());

  struct stat st;
  CHECK(stat((dir + "/.tlog").c_str(), &st) == 0);
  (void)device("create");  // cleanup
}

TEST_CASE("records survive reopen") {
  auto dir = device("reopen");
  fux::tlog::log::create(dir, 0, 10);
  auto xid = mkxid("gtrid-one");
  {
    auto log = fux::tlog::log::open(dir);
    log.append(fux::tlog::record_type::commit, &xid);
  }
  auto log = fux::tlog::log::open(dir);
  CHECK(log.outcome(&xid) == 1);
  auto recs = log.recent();
  REQUIRE(recs.size() == 1);
  CHECK(recs[0].second == 1);
  CHECK(recs[0].first == "gtrid-one");
  (void)device("reopen");
}

TEST_CASE("latest outcome wins, rollback is recorded") {
  auto dir = device("outcome");
  fux::tlog::log::create(dir, 0, 10);
  auto log = fux::tlog::log::open(dir);
  auto a = mkxid("aaa");
  auto b = mkxid("bbb");
  log.append(fux::tlog::record_type::commit, &a);
  log.append(fux::tlog::record_type::commit, &b);
  log.append(fux::tlog::record_type::rollback, &a);
  CHECK(log.outcome(&a) == 2);
  CHECK(log.outcome(&b) == 1);
  auto c = mkxid("missing");
  CHECK(log.outcome(&c) == 0);
  (void)device("outcome");
}

TEST_CASE("pages wrap around the circular window") {
  auto dir = device("wrap");
  // tiny pages area: two pages, short gtrids fill a page after ~50 records
  fux::tlog::log::create(dir, 0, 2);
  auto log = fux::tlog::log::open(dir);
  auto keep = mkxid("keep");
  auto old = mkxid("old");
  log.append(fux::tlog::record_type::commit, &old);
  for (int i = 0; i < 600; i++) {
    auto x = mkxid((std::string("x") + std::to_string(i)).c_str());
    log.append(fux::tlog::record_type::commit, &x);
  }
  log.append(fux::tlog::record_type::commit, &keep);

  auto recs = log.recent();
  CHECK(recs.size() <= 600 + 1);
  CHECK(log.outcome(&keep) == 1);
  // "old" was overwritten by the circular write
  CHECK(log.outcome(&old) == 0);
  (void)device("wrap");
}

TEST_CASE("offset shifts the page numbering") {
  auto dir = device("offset");
  fux::tlog::log::create(dir, 100, 5);
  auto log = fux::tlog::log::open(dir);
  auto xid = mkxid("shifted");
  log.append(fux::tlog::record_type::commit, &xid);
  struct stat st;
  CHECK(stat((dir + "/00000100.page").c_str(), &st) == 0);
  CHECK(log.outcome(&xid) == 1);
  (void)device("offset");
}

TEST_CASE("opening a missing or foreign device fails") {
  CHECK_THROWS(fux::tlog::log::open("no-such-dir"));
  auto dir = device("foreign");
  mkdir(dir.c_str(), 0700);
  CHECK_THROWS(fux::tlog::log::open(dir));
  (void)device("foreign");
}

TEST_CASE("heuristic records are terminal outcomes") {
  auto dir = device("heuristic");
  fux::tlog::log::create(dir, 0, 10);
  auto log = fux::tlog::log::open(dir);
  auto xid = mkxid("heur1");
  log.append(fux::tlog::record_type::commit, &xid);
  CHECK(log.outcome(&xid) == 1);
  // the branch later completes outside the log; the newer record wins
  log.append(fux::tlog::record_type::heuristic, &xid);
  CHECK(log.outcome(&xid) == 3);
  auto recs = log.recent();
  bool seen = false;
  for (auto &kv : recs) {
    if (kv.first == std::string("heur1", 5)) {
      CHECK(kv.second == 3);
      seen = true;
    }
  }
  CHECK(seen);
  (void)device("heuristic");
}
