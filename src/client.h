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
  client(mib &mibcon);
  ~client();

  int tpacall_group(int grpno, const char *svc, char *data, long len,
                    long flags);
  int tpacall_queue(queue_entry target, const char *svc, char *data, long len,
                    long flags);
  int tpgetrply(int *cd, char **data, long *len, long flags);
  int tpcancel(int cd);
  int tpsblktime(int blktime, long flags);
  int tpgblktime(long flags);

  queue_entry get_queue(const char *svc) { return repo_.get_queue(-1, svc); }

 private:
  fux::ipc::flags to_flags(long flags);
  long next_blocktime();

  mib &mibcon_;
  size_t client_;
  service_repository repo_;
  fux::ipc::msg rq;
  int rpid;
  long blocktime_next;
  long blocktime_all;
  responses cds;
  // reply descriptors whose request went through an XML2FML32 boundary
  // conversion and must be converted back before the caller sees them
  std::set<int> xml_converted;
};
