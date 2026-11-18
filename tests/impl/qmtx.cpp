// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <sys/stat.h>

#include <string>

#include <catch.hpp>

#include "qm.h"
#include "tlog.h"
#include "trx.h"

namespace fs = std::filesystem;

namespace {

std::string device(const char *name) {
  auto dir = std::string("qmtx-") + name + "-" + std::to_string(getpid());
  std::string cmd = "rm -rf " + dir;
  if (system(cmd.c_str()) != 0) {
  }
  return dir;
}

XID mkxid(const char *g) {
  XID xid = {};
  xid.formatID = 7;
  xid.gtrid_length = strlen(g);
  memcpy(xid.data, g, xid.gtrid_length);
  return xid;
}

void open_rm(const std::string &base, const std::string &qspace) {
  auto info = base + ":" + qspace;
  REQUIRE(tuxq_switch.xa_open_entry(const_cast<char *>(info.c_str()), 1,
                                    TMNOFLAGS) == XA_OK);
}

}  // namespace

TEST_CASE("staged enqueue commits into the queue") {
  auto base = device("commit");
  setenv("QMCONFIG", base.c_str(), 1);
  fux::qm::enqueue("QS", "Q1", nullptr, "one");

  open_rm(base, "QS");
  auto xid = mkxid("tx-commit");
  TPQCTL ctl = {};
  auto id = fux::qm::stage_enqueue("QS", "Q1", &ctl, "staged",
                                   fux::to_string(&xid));
  CHECK(id.find("050.") == 0);

  // message not visible before commit
  TPQCTL get = {};
  auto peek = fux::qm::dequeue("QS", "Q1", &get, true, 0);
  CHECK(peek.found);
  CHECK(peek.m.payload == "one");  // staged message not in the queue yet

  REQUIRE(tuxq_switch.xa_prepare_entry(&xid, 1, TMNOFLAGS) == XA_OK);
  REQUIRE(tuxq_switch.xa_commit_entry(&xid, 1, TMNOFLAGS) == XA_OK);

  auto first = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  CHECK(first.found);
  CHECK(first.m.payload == "one");
  auto second = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  CHECK(second.found);
  CHECK(second.m.payload == "staged");
  CHECK(!fs::is_directory(fux::qm::tx_dir("QS", fux::to_string(&xid))));
  (void)device("commit");
}

TEST_CASE("staged enqueue rolls back without trace") {
  auto base = device("rollback");
  setenv("QMCONFIG", base.c_str(), 1);
  fux::qm::enqueue("QS", "Q1", nullptr, "keep");

  open_rm(base, "QS");
  auto xid = mkxid("tx-rollback");
  TPQCTL ctl = {};
  fux::qm::stage_enqueue("QS", "Q1", &ctl, "doomed", fux::to_string(&xid));

  REQUIRE(tuxq_switch.xa_prepare_entry(&xid, 1, TMNOFLAGS) == XA_OK);
  REQUIRE(tuxq_switch.xa_rollback_entry(&xid, 1, TMNOFLAGS) == XA_OK);

  TPQCTL get = {};
  auto only = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  CHECK(only.found);
  CHECK(only.m.payload == "keep");
  CHECK(!fux::qm::dequeue("QS", "Q1", &get, false, 0).found);
  (void)device("rollback");
}

TEST_CASE("staged dequeue commits the removal, rollback restores") {
  auto base = device("dequeue");
  setenv("QMCONFIG", base.c_str(), 1);
  fux::qm::enqueue("QS", "Q1", nullptr, "msg-a");
  fux::qm::enqueue("QS", "Q1", nullptr, "msg-b");

  open_rm(base, "QS");
  TPQCTL get = {};
  auto first = fux::qm::dequeue("QS", "Q1", &get, true, 0);
  REQUIRE(first.found);
  REQUIRE(first.m.payload == "msg-a");

  auto xid = mkxid("tx-deq");
  fux::qm::stage_dequeue("QS", "Q1", first.m.id, fux::to_string(&xid));
  // gone from the queue view immediately
  auto next = fux::qm::dequeue("QS", "Q1", &get, true, 0);
  REQUIRE(next.found);
  CHECK(next.m.payload == "msg-b");

  REQUIRE(tuxq_switch.xa_prepare_entry(&xid, 1, TMNOFLAGS) == XA_OK);
  REQUIRE(tuxq_switch.xa_rollback_entry(&xid, 1, TMNOFLAGS) == XA_OK);
  auto restored = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  CHECK(restored.found);
  CHECK(restored.m.payload == "msg-a");

  auto consume_b = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  REQUIRE(consume_b.found);
  REQUIRE(consume_b.m.payload == "msg-b");
  fux::qm::enqueue("QS", "Q1", nullptr, "msg-c");
  auto third = fux::qm::dequeue("QS", "Q1", &get, true, 0);
  REQUIRE(third.found);
  REQUIRE(third.m.payload == "msg-c");
  auto xid2 = mkxid("tx-deq2");
  fux::qm::stage_dequeue("QS", "Q1", third.m.id, fux::to_string(&xid2));
  REQUIRE(tuxq_switch.xa_prepare_entry(&xid2, 1, TMNOFLAGS) == XA_OK);
  REQUIRE(tuxq_switch.xa_commit_entry(&xid2, 1, TMNOFLAGS) == XA_OK);
  CHECK(!fux::qm::dequeue("QS", "Q1", &get, false, 0).found);
  (void)device("dequeue");
}

TEST_CASE("prepare reports XA_RDONLY for a no-op transaction") {
  auto base = device("rdonly");
  setenv("QMCONFIG", base.c_str(), 1);
  fux::qm::enqueue("QS", "Q1", nullptr, "seed");
  open_rm(base, "QS");
  auto xid = mkxid("tx-none");
  CHECK(tuxq_switch.xa_prepare_entry(&xid, 1, TMNOFLAGS) == XA_RDONLY);
  (void)device("rdonly");
}

TEST_CASE("recover lists staged transactions for the TLOG resolution") {
  auto base = device("recover");
  setenv("QMCONFIG", base.c_str(), 1);
  fux::qm::enqueue("QS", "Q1", nullptr, "seed");
  open_rm(base, "QS");

  auto xid = mkxid("tx-doubt");
  TPQCTL ctl = {};
  fux::qm::stage_enqueue("QS", "Q1", &ctl, "held", fux::to_string(&xid));

  XID found[4] = {};
  int n = tuxq_switch.xa_recover_entry(found, 4, 1, TMSTARTRSCAN);
  REQUIRE(n == 1);
  CHECK(found[0].gtrid_length == xid.gtrid_length);
  CHECK(memcmp(found[0].data, xid.data, xid.gtrid_length) == 0);
  (void)device("recover");
}

TEST_CASE("crash after prepare is resolved from the tlog") {
  auto base = device("recover-tlog");
  setenv("QMCONFIG", base.c_str(), 1);
  fux::qm::enqueue("QS", "Q1", nullptr, "seed");
  open_rm(base, "QS");

  auto xid = mkxid("tx-crash");
  TPQCTL ctl = {};
  fux::qm::stage_enqueue("QS", "Q1", &ctl, "survivor", fux::to_string(&xid));
  REQUIRE(tuxq_switch.xa_prepare_entry(&xid, 1, TMNOFLAGS) == XA_OK);

  // simulate the crash decision: the commit record is in the tlog but
  // the second phase never ran
  auto tlogdir = base + "-tlog";
  fux::tlog::log::create(tlogdir, 0, 10);
  {
    auto log = fux::tlog::log::open(tlogdir);
    log.append(fux::tlog::record_type::commit, &xid);
  }

  // recovery scan: what the TMS does at boot
  int n = 0;
  XID xids[4] = {};
  n = tuxq_switch.xa_recover_entry(xids, 4, 1, TMSTARTRSCAN);
  REQUIRE(n == 1);
  auto log = fux::tlog::log::open(tlogdir);
  int outcome = log.outcome(&xids[0]);
  REQUIRE(outcome == 1);
  if (outcome == 1) {
    tuxq_switch.xa_commit_entry(&xids[0], 1, TMNOFLAGS);
  } else {
    tuxq_switch.xa_rollback_entry(&xids[0], 1, TMNOFLAGS);
  }

  TPQCTL get = {};
  auto got = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  CHECK(got.found);
  CHECK(got.m.payload == "seed");
  auto surv = fux::qm::dequeue("QS", "Q1", &get, false, 0);
  CHECK(surv.found);
  CHECK(surv.m.payload == "survivor");
  (void)device("recover-tlog");
}
