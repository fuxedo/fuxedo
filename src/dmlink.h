// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#pragma once
// Domain/workstation link engine: a single-threaded select() poll loop
// built on the classic select() idioms: one master fd_set copied per
// call, fd-indexed connection state pre-allocated by rlimit, accept()
// until EAGAIN, at most select()'s returned count of events processed
// per iteration.
//
// Frame codec: [magic:4 "FUX1"][type:1][len:4 BE][payload]. TLS wraps the
// byte stream once the persisted-EC-key + trust-on-first-use pin
// handshake lands; until then the link is plain.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct timeval;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

namespace fux::link {

// Link identity and trust store. A persisted EC P-256 identity key and
// self-signed certificate live in `dir` (created on first use, mode
// 0600); `dir`/pins holds the trust-on-first-use fingerprint cache,
// one file per peer. No certificate authority anywhere: the
// application binds peer fingerprints to names after the handshake.
class security {
 public:
  security(const std::string &dir, const std::string &cn);
  ~security();
  security(const security &) = delete;
  security &operator=(const security &) = delete;

  SSL_CTX *server_ctx() { return server_; }
  SSL_CTX *client_ctx() { return client_; }

  // lowercase sha256 hex of the peer certificate, empty when absent
  static std::string fingerprint(SSL *ssl);
  bool pin_load(const std::string &name, std::string &fp) const;
  void pin_store(const std::string &name, const std::string &fp) const;

 private:
  std::string dir_;
  SSL_CTX *server_ = nullptr;
  SSL_CTX *client_ = nullptr;
};

enum frame_type : uint8_t {
  hello = 1,     // access point id + supported version
  services = 2,  // advertised service list
  request = 3,   // service call: name + exported payload + corrid
  reply = 4,     // corrid + rval + exported payload
  heartbeat = 5,
  bye = 6,
  login = 7,  // workstation client login; the ack reuses the type
};

struct frame {
  frame_type type;
  std::vector<char> payload;
};

class engine {
 public:
  // Called with each complete inbound frame per connection key.
  std::function<void(int conn, frame)> on_frame;
  std::function<void(int conn)> on_accept;  // link up (handshake done)
  std::function<void(int conn)> on_close;
  // Decide whether a completed handshake is acceptable; `dialed` is
  // true for outbound connections. Returning false drops the link.
  std::function<bool(int conn, const std::string &fp, bool dialed)> on_verify;

  engine();
  ~engine();
  engine(const engine &) = delete;
  engine &operator=(const engine &) = delete;

  int listen(const std::string &host, uint16_t port);
  int connect(const std::string &host, uint16_t port);
  void close(int conn);
  void cut(int conn);  // drop a connection immediately (protocol error)
  bool queue(int conn, frame f);  // frame out when writable; false = gone

  // TLS on all new connections; the security object must outlive the
  // engine and must not be reconfigured afterwards.
  void secure(security *sec) { sec_ = sec; }

  // one poll iteration; returns after processing ready events or on
  // timeout; nullptr timeout blocks like plain select()
  void poll(struct timeval *timeout = nullptr);
  int maxfd() const { return maxfd_; }

 private:
  struct conn;
  void do_accept(int listener);
  void do_read(int fd);
  void do_write(int fd);
  void drop(int fd);
  void arm(int fd);
  void start_tls(int fd, bool server_side);
  bool pump_handshake(int fd);
  void pump_in(int fd);
  void pump_out(int fd);
  bool flush_raw(int fd);
  void drain_wbio(int fd);
  void parse_frames(int fd);
  void update_wantwr(int fd);

  conn *conns_ = nullptr;  // indexed by fd, pre-allocated
  int capacity_ = 0;
  int maxfd_ = -1;
  unsigned char *master_ = nullptr;  // bitset of armed fds
  unsigned char *ready_ = nullptr;   // select() output copy
  unsigned char *wantwr_ = nullptr;  // fds with pending outbound frames
  unsigned char *wrout_ = nullptr;   // select() write-set output copy
  size_t nbits_ = 0;
  std::vector<int> listeners_;
  security *sec_ = nullptr;
};

}  // namespace fux::link
