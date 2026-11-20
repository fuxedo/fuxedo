#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <set>
#include <string>

#include <xatmi.h>

#include "ipc.h"
#include "mib.h"
#include "resp.h"
#include "svcrepo.h"

// An ATMI client context bound to the local bulletin board: one accesser
// slot and one reply queue. The free client functions in client.cpp route
// through a thread-local instance; multi-connection servers (WSL) hold one
// instance per remote client instead.
class client {
 public:
  client(mib &mibcon, const char *cltname = nullptr,
         const char *usrname = nullptr);
  ~client();

  int tpacall_group(int grpno, const char *svc, char *data, long len,
                    long flags);
  int tpacall_queue(queue_entry target, const char *svc, char *data, long len,
                    long flags);
  int tpgetrply(int *cd, char **data, long *len, long flags);
  int tpcancel(int cd);
  int tpsblktime(int blktime, long flags);
  int tpgblktime(long flags);

  using unsol_handler_t = void (*)(char *data, long len, long flags);
  unsol_handler_t setunsol(unsol_handler_t f) {
    auto prev = unsol_handler_;
    unsol_handler_ = f;
    return prev;
  }
  // deliver one unsolicited message to the installed handler; the count
  // delivered (0 when no handler is installed)
  size_t dispatch_unsol(fux::ipc::msg &res);
  // drain the reply queue for unsolicited messages, dispatching each;
  // replies and markers are kept or discarded like in drain_late_replies
  int chkunsol();

  // conversational client API
  int tpconnect(const char *svc, char *data, long len, long flags);
  int tpsend(int cd, char *data, long len, long flags, long *revent);
  int tprecv(int cd, char **data, long *len, long flags, long *revent);
  int tpdiscon(int cd);
  // best-effort DISCONIMM for every open conversation (tpterm cleanup)
  void disconnect_convs();

  queue_entry get_queue(const char *svc) { return repo_.get_queue(-1, svc); }

 private:
  fux::ipc::flags to_flags(long flags);
  long next_blocktime();
  // drain late replies and timeout markers out of the reply queue; the
  // platform sweeps on a cadence like this too (about every fifth call)
  void drain_late_replies();

  mib &mibcon_;
  size_t client_;
  service_repository repo_;
  fux::ipc::msg rq;
  int rpid;
  int sweep_ = 0;
  long blocktime_next;
  long blocktime_all;
  responses cds;
  // reply descriptors whose request went through an XML2FML32 boundary
  // conversion and must be converted back before the caller sees them
  std::set<int> xml_converted;
  unsol_handler_t unsol_handler_ = nullptr;
  // conversational connections owned by this context; descriptors start
  // at fux::ipc::conv_base so they never collide with reply descriptors
  // or their message types on the reply queue
  struct conv_state {
    int msqid;     // the service's request queue
    bool open;     // terminal event not yet received
    bool send_mine;  // half-duplex send permission
  };
  std::map<int, conv_state> convs_;
};
