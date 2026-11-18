#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <map>
#include <string>
#include <vector>

#include <openssl/ssl.h>

#include "dmlink.h"

namespace fux::ws {

// A workstation client connection: TLS to a WSL listener, TPINIT login,
// tpcall request/reply correlation by descriptor. Selected at runtime
// when the WSNADDR environment variable is set; no local TUXCONFIG or
// bulletin board is touched.
class wsconn {
 public:
  wsconn();
  ~wsconn();
  wsconn(const wsconn &) = delete;
  wsconn &operator=(const wsconn &) = delete;

  // connects, does the TLS handshake with trust-on-first-use pinning,
  // and logs in; returns 0 or -1 with tperrno set
  int login(const char *cltname, const char *usrname, const char *grpname,
            const char *passwd);
  void logout();

  int tpacall(const char *svc, char *data, long len, long flags);
  int tpgetrply(int *cd, char **data, long *len, long flags);
  int tpcancel(int cd);
  int tpsblktime(int blktime, long flags);
  int tpgblktime(long flags);

 private:
  bool send_frame(fux::link::frame_type type, std::vector<char> payload);
  bool recv_frame(fux::link::frame_type *type, std::vector<char> *payload,
                  long deadline_ms);
  bool wait_ssl(int rc, long deadline_ms);
  long next_blocktime();

  int fd_ = -1;
  SSL *ssl_ = nullptr;
  fux::link::security *sec_ = nullptr;
  std::string host_;
  uint16_t port_ = 0;
  uint32_t next_cd_ = 1;
  long blocktime_next_ = 0;
  long blocktime_all_ = 0;
  // replies that arrived for descriptors nobody waited on yet
  std::map<uint32_t, std::vector<char>> buffered_;
};

// WSNADDR mode; nullptr when the variable is not set
const char *ws_naddr();
wsconn &get_wsconn();

}  // namespace fux::ws
