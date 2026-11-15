// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <stdexcept>

#include <atmi.h>
#include <cstring>
#include <filesystem>
#include <unistd.h>

#include "../src/qm.h"

namespace fs = std::filesystem;

TEST_CASE("openinfo", "[qm]") {
  REQUIRE(tuxq_switch.xa_open_entry("", 1, 0) == TMER_INVAL);
  REQUIRE(tuxq_switch.xa_open_entry("/tmp", 1, 0) == TMER_INVAL);
  REQUIRE(tuxq_switch.xa_open_entry("/tmp:q", 1, 0) == TMER_INVAL);
  REQUIRE(tuxq_switch.xa_close_entry(nullptr, 1, 0) == XA_OK);
}

namespace {

struct qm_test_dir {
  fs::path dir;
  qm_test_dir() {
    dir = fs::absolute("tmp/qmtest-" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    setenv("QMCONFIG", dir.c_str(), 1);
  }
  ~qm_test_dir() { fs::remove_all(dir); }
};

TPQCTL enqueue_msg(const char *qname, const char *text, long flags = 0,
                   long prio = 0) {
  TPQCTL ctl = {};
  ctl.flags = flags;
  ctl.priority = prio;
  auto data = tpalloc(const_cast<char *>("STRING"), nullptr, strlen(text) + 1);
  strcpy(data, text);
  int rc = tpenqueue(const_cast<char *>("QS1"), const_cast<char *>(qname),
                     &ctl, data, 0, TPNOFLAGS);
  tpfree(data);
  REQUIRE(rc == 0);
  return ctl;
}

std::string dequeue_msg(const char *qname, TPQCTL *ctl = nullptr,
                        long flags = TPNOFLAGS) {
  TPQCTL local = {};
  auto out = tpalloc(const_cast<char *>("STRING"), nullptr, 10);
  long len = 0;
  int rc = tpdequeue(const_cast<char *>("QS1"), const_cast<char *>(qname),
                     ctl == nullptr ? &local : ctl, &out, &len, flags);
  REQUIRE(rc == 0);
  std::string result(out);
  tpfree(out);
  return result;
}

}  // namespace

TEST_CASE("messages can be enqueued and dequeued", "[qm]") {
  qm_test_dir qm;

  enqueue_msg("Q1", "hello");
  REQUIRE(dequeue_msg("Q1") == "hello");

  // empty queue fails with QMENOMSG
  TPQCTL ctl = {};
  auto out = tpalloc(const_cast<char *>("STRING"), nullptr, 10);
  long len = 0;
  REQUIRE(tpdequeue(const_cast<char *>("QS1"), const_cast<char *>("Q1"), &ctl,
                    &out, &len, TPNOFLAGS) == -1);
  REQUIRE(ctl.diagnostic == QMENOMSG);
  tpfree(out);
}

TEST_CASE("queues are created when needed", "[qm]") {
  qm_test_dir qm;
  enqueue_msg("NEWQ", "created on demand");
  REQUIRE(fs::exists(qm.dir / "QS1" / "NEWQ"));
  REQUIRE(dequeue_msg("NEWQ") == "created on demand");
}

TEST_CASE("messages dequeue in priority order", "[qm]") {
  qm_test_dir qm;
  enqueue_msg("PRIO", "normal", TPQPRIORITY, 50);
  enqueue_msg("PRIO", "top", TPQTOP);
  enqueue_msg("PRIO", "high", TPQPRIORITY, 10);

  REQUIRE(dequeue_msg("PRIO") == "top");
  REQUIRE(dequeue_msg("PRIO") == "high");
  REQUIRE(dequeue_msg("PRIO") == "normal");
}

TEST_CASE("messages can be selected by corrid", "[qm]") {
  qm_test_dir qm;
  enqueue_msg("SEL", "first");

  TPQCTL enq = {};
  enq.flags = TPQCORRID;
  strcpy(enq.corrid, "myid");
  auto data = tpalloc(const_cast<char *>("STRING"), nullptr, 7);
  strcpy(data, "third");
  REQUIRE(tpenqueue(const_cast<char *>("QS1"), const_cast<char *>("SEL"),
                    &enq, data, 0, TPNOFLAGS) == 0);
  tpfree(data);

  TPQCTL get = {};
  get.flags = TPQGETBYCORRID;
  strcpy(get.corrid, "myid");
  REQUIRE(dequeue_msg("SEL", &get) == "third");
  REQUIRE(dequeue_msg("SEL") == "first");
}

TEST_CASE("dot-prefixed queue names are rejected", "[qm]") {
  qm_test_dir qm;
  TPQCTL ctl = {};
  auto data = tpalloc(const_cast<char *>("STRING"), nullptr, 4);
  strcpy(data, "x");
  REQUIRE(tpenqueue(const_cast<char *>("QS1"), const_cast<char *>(".hidden"),
                    &ctl, data, 0, TPNOFLAGS) == -1);
  REQUIRE(tperrno == TPEINVAL);
  tpfree(data);
}

TEST_CASE("queue operations in transaction mode are rejected", "[qm]") {
  // Requires an attached domain (tpbegin needs TUXCONFIG); covered by
  // domain-level tests instead. fux::tx::tx_active() guards the check so
  // standalone queue use works without a domain.
}
