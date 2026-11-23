// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// TMQFORWARD server: dequeues messages from a queue and forwards each to
// the service named by the queue. Booted like an application server with
//   TMQFORWARD SRVGRP=QUE SRVID=5 CLOPT="-- -q STRING [-i threads]"
// in a group whose OPENINFO is "TUXEDO/QM:<device>:<qspace>".
//
// Each forward runs in one transaction: the dequeue stages on this
// group's QM branch and the target service joins the same transaction,
// so its work commits or aborts together with the dequeue. A failed
// call aborts - the message returns to the queue and is redelivered
// after the queue's retry delay. When a message's delivery attempts
// exceed the queue's retries limit it moves to the message's failure
// queue, or to the queue space error queue, with a ULOG note; with no
// such queue it is dropped. The platform keeps the delivery count in
// the message header (surviving restarts); this implementation counts
// in memory, which only changes behavior across a TMQFORWARD restart.

#include <xa.h>

#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <userlog.h>

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "qm.h"
#include "tmqwire.h"
#include "trx.h"
#include "servermain.h"

static std::string g_qspace;
static std::string g_queue;
static int g_threads = 1;
static long g_retries = 0;      // 0 = redeliver until the queue gives up
static long g_retrydelay = 0;   // seconds between deliveries

// delivery attempts per message id; the platform persists this in the
// message header, we count per TMQFORWARD run
static std::map<std::string, int> g_attempts;
static std::mutex g_attempts_mu;

static std::string error_queue() {
  auto meta =
      fux::qm::read_meta(fux::qm::qspace_dir(g_qspace) / fux::qm::META_FILE);
  auto it = meta.find("errque");
  return it == meta.end() ? "" : it->second;
}

static void queue_limits() {
  auto meta = fux::qm::read_meta(fux::qm::queue_dir(g_qspace, g_queue) /
                                 fux::qm::META_FILE);
  auto it = meta.find("retries");
  if (it != meta.end()) {
    g_retries = atol(it->second.c_str());
  }
  it = meta.find("retrydelay");
  if (it != meta.end()) {
    g_retrydelay = atol(it->second.c_str());
  }
}

static void wait_between_deliveries() {
  std::this_thread::sleep_for(std::chrono::seconds(g_retrydelay > 0
                                                       ? g_retrydelay
                                                       : 1));
}

// Gives up on a message after the queue's retries limit: moves it to
// its failure queue when one exists (the message's own failurequeue
// first, then the queue space error queue).
static void dead_letter(const TPQCTL *ctl, const char *data, long len) {
  std::string dest = ctl->failurequeue;
  if (dest.empty()) {
    dest = error_queue();
  }
  if (dest.empty()) {
    userlog("TMQFORWARD: delivery attempts exhausted; no failure queue, "
            "message dropped");
    return;
  }
  TPQCTL ectl = {};
  std::string payload;
  if (data != nullptr && len > 0) {
    payload.assign(data, len);
  }
  fux::tmq::exec_enqueue(g_qspace.c_str(), dest.c_str(), &ectl, payload);
  userlog("TMQFORWARD: delivery attempts exhausted; message moved to %s",
          dest.c_str());
}

static void forward_loop() {
  while (true) {
    // one transaction per forward: dequeue + service work commit or
    // abort together
    if (tpbegin(120, 0) == -1) {
      userlog("TMQFORWARD: tpbegin failed: %s", tpstrerror(tperrno));
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    TPQCTL ctl = {};
    ctl.flags = TPQREPLYQ | TPQMSGID;  // learn reply queue and message id
    char *data = tpalloc(const_cast<char *>("CARRAY"), nullptr, 1024);
    long len = 0;
    int rc = fux::tmq::exec_dequeue(g_qspace.c_str(), g_queue.c_str(), &ctl,
                                    &data, &len, TPQWAIT);
    if (rc == -1) {
      tpfree(data);
      tpabort(0);
      bool empty = tperrno == TPEDIAGNOSTIC &&
                   ctl.diagnostic == QMENOMSG;
      if (empty || tperrno == TPETIME) {
        continue;  // no message yet; keep listening
      }
      userlog("TMQFORWARD: dequeue from %s failed: %s", g_queue.c_str(),
              tpstrerror(tperrno));
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    std::string msgid(ctl.msgid, strnlen(ctl.msgid, TMMSGIDLEN));
    int fret = ::tpcall(const_cast<char *>(g_queue.c_str()), data, len,
                        reinterpret_cast<char **>(&data), &len, 0);
    if (fret == -1) {
      // the abort below resets tperrno: capture the reason first
      std::string why = tpstrerror(tperrno);
      tpabort(0);  // the message returns to the queue for a retry
      int att = 0;
      {
        std::lock_guard<std::mutex> lock(g_attempts_mu);
        att = ++g_attempts[msgid];
      }
      userlog("TMQFORWARD: forward to %s failed: %s (delivery %d)",
              g_queue.c_str(), why.c_str(), att);
      if (g_retries > 0 && att > g_retries) {
        {
          std::lock_guard<std::mutex> lock(g_attempts_mu);
          g_attempts.erase(msgid);
        }
        dead_letter(&ctl, data, len);
      } else {
        wait_between_deliveries();
      }
      tpfree(data);
      continue;
    }
    if (tpcommit(0) == -1) {
      userlog("TMQFORWARD: commit of %s forward failed: %s",
              g_queue.c_str(), tpstrerror(tperrno));
      tpfree(data);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(g_attempts_mu);
      g_attempts.erase(msgid);
    }
    if (ctl.replyqueue[0] != '\0' && data != nullptr && len > 0) {
      // the reply goes to the queue named by the originator
      std::string wire;
      long used = fux::mem::bufsize(data, len);
      if (used > 0) {
        wire.resize(used);
        if (tpexport(data, len, &wire[0], &used, 0) != -1) {
          wire.resize(used);
          TPQCTL rctl = {};
          fux::tmq::exec_enqueue(g_qspace.c_str(), ctl.replyqueue, &rctl,
                                 wire);
        }
      }
    }
    tpfree(data);
  }
}

// OPENINFO "TUXEDO/QM:<device>:<qspace>" -> QMCONFIG=<device>, qspace
static bool setup_qspace() {
  auto &m = getmib();
  auto groups = m.groups();
  for (size_t i = 0; i < groups.length(); i++) {
    auto &g = groups.at(i);
    if (g.grpno != fux::tx::grpno) {
      continue;
    }
    std::string info = g.openinfo;
    const std::string prefix = "TUXEDO/QM:";
    if (info.compare(0, prefix.size(), prefix) != 0) {
      userlog("TMQFORWARD requires OPENINFO starting with TUXEDO/QM:");
      return false;
    }
    auto rest = info.substr(prefix.size());
    auto colon = rest.find(':');
    if (colon == std::string::npos) {
      userlog("TMQFORWARD OPENINFO must be TUXEDO/QM:<device>:<qspace>");
      return false;
    }
    setenv("QMCONFIG", rest.substr(0, colon).c_str(), 1);
    g_qspace = rest.substr(colon + 1);
    fux::qm::validate_qspace_name(g_qspace);
    return true;
  }
  userlog("TMQFORWARD group %d not found", fux::tx::grpno);
  return false;
}

int tpsvrinit(int argc, char *argv[]) {
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
      g_queue = argv[++i];
    } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
      g_threads = std::max(1, atoi(argv[++i]));
    }
  }
  if (g_queue.empty()) {
    userlog("TMQFORWARD: -q <queue> is required");
    return -1;
  }
  if (!setup_qspace()) {
    return -1;
  }
  queue_limits();
  if (tpopen() == -1) {
    userlog("TMQFORWARD: tpopen failed: %s", tpstrerror(tperrno));
    return -1;
  }
  for (int i = 0; i < g_threads; i++) {
    std::thread t(forward_loop);
    t.detach();
  }
  return 0;
}

void tpsvrdone(void) { tpclose(); }

static struct tmdsptchtbl_t _tmdsptchtbl[] = {
    {NULL, NULL, NULL, 0, 0, NULL}};
static struct tmsvrargs_t tmsvrargs =
    fuxedo_server_args(&tuxq_switch, _tmdsptchtbl, true);
struct tmsvrargs_t *_tmgetsvrargs() { return &tmsvrargs; }

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
