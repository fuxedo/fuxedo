// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// TMQFORWARD server: dequeues messages from a queue and forwards each to
// the service named by the queue. Booted like an application server with
//   TMQFORWARD SRVGRP=QUE SRVID=5 CLOPT="-- -q STRING [-i threads]"
// in a group whose OPENINFO is "TUXEDO/QM:<device>:<qspace>". On forward
// failure the message is moved to the queue space error queue when one is
// configured, otherwise it is dropped with a ULOG note.

#include <xa.h>

#include <atmi.h>
#include <stdio.h>
#include <string.h>
#include <userlog.h>

#include <string>
#include <thread>
#include <vector>

#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "qm.h"
#include "tmqwire.h"
#include "trx.h"

extern "C" {
int _tmrunserver(int);
extern int _tmbuilt_with_thread_option;
}

static std::string g_qspace;
static std::string g_queue;
static int g_threads = 1;

static std::string error_queue() {
  auto meta =
      fux::qm::read_meta(fux::qm::qspace_dir(g_qspace) / fux::qm::META_FILE);
  auto it = meta.find("errque");
  return it == meta.end() ? "" : it->second;
}

static void forward_loop() {
  while (true) {
    TPQCTL ctl = {};
    ctl.flags = TPQREPLYQ;  // learn the message's reply queue
    char *data = tpalloc(const_cast<char *>("CARRAY"), nullptr, 1024);
    long len = 0;
    int rc = fux::tmq::exec_dequeue(g_qspace.c_str(), g_queue.c_str(), &ctl,
                                    &data, &len, TPQWAIT);
    if (rc == -1) {
      tpfree(data);
      if (tperrno == TPETIME) {
        continue;  // TPQWAIT timed out; keep listening
      }
      userlog("TMQFORWARD: dequeue from %s failed: %s", g_queue.c_str(),
              tpstrerror(tperrno));
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    int fret = ::tpcall(const_cast<char *>(g_queue.c_str()), data, len,
                        reinterpret_cast<char **>(&data), &len, TPNOTRAN);
    if (fret == -1) {
      userlog("TMQFORWARD: forward to %s failed: %s", g_queue.c_str(),
              tpstrerror(tperrno));
      auto errq = error_queue();
      if (!errq.empty()) {
        TPQCTL ectl = {};
        std::string payload;
        if (data != nullptr && len > 0) {
          payload.assign(data, len);
        }
        fux::tmq::exec_enqueue(g_qspace.c_str(), errq.c_str(), &ectl, payload);
      }
    } else if (ctl.replyqueue[0] != '\0' && data != nullptr && len > 0) {
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
static struct tmsvrargs_t tmsvrargs = {
    &tuxq_switch, _tmdsptchtbl, 0,    tpsvrinit, tpsvrdone, _tmrunserver,
    NULL,         NULL,         NULL, NULL,      tprminit,  tpsvrthrinit,
    tpsvrthrdone, NULL};

struct tmsvrargs_t *_tmgetsvrargs() {
  return &tmsvrargs;
}

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
