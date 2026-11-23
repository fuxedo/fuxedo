// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// TMQUEUE server: performs enqueue/dequeue for one queue space on behalf
// of clients. Booted like an application server with
//   TMQUEUE SRVGRP=QUE SRVID=1 CLOPT="-s QSPACE:TMQUEUE --"
// in a group whose OPENINFO is "TUXEDO/QM:<device>:<qspace>". The queue
// space is bound to the group resource manager, so requests arriving in a
// caller's transaction stage with it.

#include <xa.h>

#include <atmi.h>
#include <fml32.h>
#include <stdio.h>
#include <string.h>
#include <tpadm.h>
#include <userlog.h>

#include <string>
#include <vector>

#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "qm.h"
#include "tmqwire.h"
#include "servermain.h"

static std::string g_qspace;
static std::vector<std::string> g_services;

static void TMQ(TPSVCINFO *svcinfo) {
  fux::fml32buf rq(svcinfo);
  fux::fml32buf rs;

  auto op = rq.get<long>(fux::tmq::f_op(), 0);
  std::string qname = rq.getbytes(fux::tmq::f_qname(), 0);
  std::string ctlbytes = rq.getbytes(fux::tmq::f_ctl(), 0);

  TPQCTL ctl = {};
  std::copy_n(ctlbytes.data(), std::min(ctlbytes.size(), sizeof(ctl)),
              reinterpret_cast<char *>(&ctl));

  int rc = 0;
  if (op == fux::tmq::enqueue) {
    std::string payload;
    if (rq.count(fux::tmq::f_data()) > 0) {
      payload = rq.getbytes(fux::tmq::f_data(), 0);
    }
    rc = fux::tmq::exec_enqueue(g_qspace.c_str(), qname.c_str(), &ctl,
                                payload);
  } else if (op == fux::tmq::dequeue) {
    long flags = rq.get<long>(fux::tmq::f_flags(), 0, 0L);
    char *data = tpalloc(const_cast<char *>("CARRAY"), nullptr, 1024);
    long len = 0;
    rc = fux::tmq::exec_dequeue(g_qspace.c_str(), qname.c_str(), &ctl, &data,
                                &len, flags);
    if (rc == 0 && len > 0) {
      // exec_dequeue tpimport()ed the message into a typed buffer; the
      // wire carries export bytes
      std::string wire(len + 128, '\0');
      long elen = wire.size();
      if (tpexport(data, len, &wire[0], &elen, 0) == -1) {
        rc = -1;
        ctl.diagnostic = QMEOS;
      } else {
        wire.resize(elen);
        rs.put(fux::tmq::f_data(), 0, wire);
      }
    }
    if (data != nullptr) {
      tpfree(data);
    }
  } else {
    TPERROR(TPEINVAL, "unknown queue operation %ld", op);
    rc = -1;
  }

  rs.put(fux::tmq::f_ctl(), 0,
         std::string(reinterpret_cast<char *>(&ctl), sizeof(ctl)));
  fux::tpreturn(rc == 0 ? TPSUCCESS : TPFAIL, ctl.diagnostic, rs);
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
      userlog("TMQUEUE requires OPENINFO starting with TUXEDO/QM:");
      return false;
    }
    auto rest = info.substr(prefix.size());
    auto colon = rest.find(':');
    if (colon == std::string::npos) {
      userlog("TMQUEUE OPENINFO must be TUXEDO/QM:<device>:<qspace>");
      return false;
    }
    setenv("QMCONFIG", rest.substr(0, colon).c_str(), 1);
    g_qspace = rest.substr(colon + 1);
    fux::qm::validate_qspace_name(g_qspace);
    return true;
  }
  userlog("TMQUEUE group %d not found", fux::tx::grpno);
  return false;
}

int tpsvrinit(int argc, char *argv[]) {
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      g_services.push_back(argv[++i]);
    }
  }
  if (g_services.empty()) {
    userlog("TMQUEUE: no service advertised, use -s <qspace>:TMQUEUE");
    return -1;
  }
  if (!setup_qspace()) {
    return -1;
  }
  if (tpopen() == -1) {
    userlog("TMQUEUE: tpopen failed: %s", tpstrerror(tperrno));
    return -1;
  }
  for (auto &svc : g_services) {
    if (tpadvertise(const_cast<char *>(svc.c_str()), TMQ) == -1) {
      userlog("TMQUEUE: failed to advertise %s: %s", svc.c_str(),
              tpstrerror(tperrno));
      return -1;
    }
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
  // queue waits are served client-side (retry loop), never here
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
