// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// WSL server: the workstation listener, a fuxedo-provided binary booted
// in its own group like an application server:
//
//   WSL SRVGRP=WS SRVID=1 CLOPT="-n //localhost:8765 --"
//
// Workstation clients (WSNADDR set, no local TUXCONFIG) connect over
// TLS with trust-on-first-use pins, log in with their TPINIT identity,
// and issue service requests; this process bridges them into the local
// bulletin board, one client context per connection. Single threaded:
// socket events drive the engine from the dispatch thread via the poll
// hook, and the idle timer drains pending replies out of the client
// queues.

#include <signal.h>
#include <sys/time.h>

#include <atmi.h>
#include <fml32.h>
#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "client.h"
#include "dmlink.h"
#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "trx.h"
#include "wire.h"

extern "C" {
int _tmrunserver(int);
extern int _tmbuilt_with_thread_option;
}

namespace {

using steady = std::chrono::steady_clock;

struct wslink {
  std::unique_ptr<client> cl;
  std::string cltname;
  std::set<int> pending;  // outstanding reply descriptors
  // the connection's current transaction (dotted xid, empty when none);
  // this process is the transaction's agent inside the domain
  std::string tx;
  uint32_t tx_timeout = 0;       // seconds, from the begin request
  steady::time_point tx_started;  // when the begin arrived
};

fux::link::engine eng;
std::unique_ptr<fux::link::security> sec;
std::map<int, wslink> links;
steady::time_point now;
size_t max_clients = 0;  // 0 = unlimited (WSL -x or machine MAXWSCLIENTS)

// transaction control operations (mirrors the client's wire constants)
constexpr uint16_t kTxBegin = 0;
constexpr uint16_t kTxCommit = 1;
constexpr uint16_t kTxAbort = 2;

void send_frame(int fd, fux::link::frame_type type, std::vector<char> payload) {
  eng.queue(fd, {type, std::move(payload)});
}

void on_accept(int fd) {
  links[fd].cltname.clear();
}

void rollback_tx(wslink &l);

void on_close(int fd) {
  auto it = links.find(fd);
  if (it != links.end() && !it->second.tx.empty()) {
    // the client went away mid-transaction: roll the branch back
    rollback_tx(it->second);
  }
  links.erase(fd);
}

// Aborts the connection's transaction and forgets it. Shared by the
// disconnect path and the begin-timeout reaper.
void rollback_tx(wslink &l) {
  try {
    XID xid = fux::to_xid(l.tx);
    fux::tm::coordinate(&xid, fux::tm::rollback);
    auto &tbl = getmib().transactions();
    tbl.end(tbl.find(xid));
  } catch (const std::exception &e) {
    userlog("WSL: transaction rollback failed: %s", e.what());
  }
  l.tx.clear();
  l.tx_timeout = 0;
}

bool on_verify_bridge(int fd, const std::string &fp, bool dialed) {
  (void)fp;
  (void)dialed;
  (void)fd;
  // clients authenticate with their TPINIT identity; the link itself is
  // protected by TLS, client certificate pinning is not required
  return true;
}

// Exports a typed buffer into wire bytes; the export always carries at
// least the type header so the far side's import never sees a zero-byte
// payload (which the import path cannot express).
bool export_buf(char *data, long len, std::vector<char> &out) {
  if (data == nullptr || len < 0) {
    return false;
  }
  out.assign(static_cast<size_t>(len) + 1024, '\0');
  long olen = static_cast<long>(out.size());
  if (tpexport(data, len, out.data(), &olen, 0) == -1) {
    if (tperrno != TPELIMIT) {
      return false;
    }
    out.resize(static_cast<size_t>(olen));
    olen = static_cast<long>(out.size());
    if (tpexport(data, len, out.data(), &olen, 0) == -1) {
      return false;
    }
  }
  out.resize(static_cast<size_t>(olen));
  return true;
}

std::vector<char> dummy_export() {
  std::vector<char> out;
  char *b = tpalloc(const_cast<char *>("CARRAY"), nullptr, 1);
  if (b != nullptr) {
    export_buf(b, 0, out);
    tpfree(b);
  }
  return out;
}

void handle_login(int fd, fux::link::frame &f) {
  fux::wire::reader r(f.payload);
  std::string cltname, usrname, grpname, passwd;
  if (!r.str(cltname) || !r.str(usrname) || !r.str(grpname) || !r.str(passwd)) {
    eng.cut(fd);
    return;
  }
  (void)usrname;
  (void)grpname;
  (void)passwd;  // SECURITY NONE: the identity is carried for later

  size_t logged_in = 0;
  for (auto &[fd, l] : links) {
    if (l.cl != nullptr) {
      logged_in++;
    }
  }
  if (max_clients > 0 && logged_in >= max_clients) {
    userlog("WSL: client limit %zu reached, refusing %s", max_clients,
            cltname.c_str());
    std::vector<char> ack;
    fux::wire::writer w(ack);
    w.u32(static_cast<uint32_t>(TPELIMIT));
    send_frame(fd, fux::link::login, std::move(ack));
    return;
  }

  try {
    auto &l = links[fd];
    l.cltname = cltname;
    l.cl = std::make_unique<client>(getmib());
  } catch (const std::exception &e) {
    userlog("WSL: cannot create client context: %s", e.what());
    eng.cut(fd);
    return;
  }
  std::vector<char> ack;
  fux::wire::writer w(ack);
  w.u32(0);
  send_frame(fd, fux::link::login, std::move(ack));
  userlog("WSL: client %s logged in on fd %d", cltname.c_str(), fd);
}

void handle_request(int fd, fux::link::frame &f) {
  auto it = links.find(fd);
  if (it == links.end() || it->second.cl == nullptr) {
    eng.cut(fd);
    return;
  }
  auto &l = it->second;

  fux::wire::reader r(f.payload);
  std::string svc;
  uint32_t cd, flags;
  std::vector<char> payload;
  if (!r.str(svc) || !r.u32(cd) || !r.u32(flags) || !r.bytes(payload)) {
    eng.cut(fd);
    return;
  }
  if (svc.empty()) {
    eng.cut(fd);
    return;
  }

  char *buf = tpalloc(const_cast<char *>("CARRAY"), nullptr,
                      static_cast<long>(payload.size()) + 1024);
  auto refuse = [&](long rval) {
    std::vector<char> dummy = dummy_export();
    std::vector<char> reply;
    fux::wire::writer w(reply);
    w.u32(cd);
    w.u32(static_cast<uint32_t>(rval));
    w.u64(0);
    w.bytes(dummy.data(), dummy.size());
    send_frame(fd, fux::link::reply, std::move(reply));
  };
  if (buf == nullptr) {
    refuse(tperrno);
    return;
  }
  long ilen = 0;
  if (!payload.empty() &&
      tpimport(payload.data(), static_cast<long>(payload.size()), &buf, &ilen,
               0) == -1) {
    tpfree(buf);
    refuse(tperrno);
    return;
  }
  ilen -= fux::mem::headersize();

  // transactional requests carry the connection's transaction id after
  // the payload; links that predate workstation transactions omit it
  std::string gtrid;
  r.str(gtrid);
  bool intx = (flags & TPTRAN) != 0;
  if (intx && gtrid != l.tx) {
    // a client may only run the transaction it began on this connection
    userlog("WSL: transactional request for %s refused (%s)", svc.c_str(),
            gtrid.empty() ? "no transaction id"
                          : "unknown transaction for this connection");
    tpfree(buf);
    refuse(TPEABORT);
    return;
  }

  long call_flags = intx ? 0 : TPNOTRAN;
  if (flags & TPNOREPLY) {
    call_flags |= TPNOREPLY;
  }
  if (intx) {
    try {
      fux::tx_adopt(fux::to_xid(gtrid));
      userlog("WSL: %s runs in transaction %s (level %d)", svc.c_str(),
              gtrid.c_str(), fux::tx::transactional() ? 1 : 0);
    } catch (const std::exception &e) {
      tpfree(buf);
      userlog("WSL: cannot adopt transaction for %s: %s", svc.c_str(),
              e.what());
      refuse(TPEABORT);
      return;
    }
  }
  int rcd = l.cl->tpacall_group(-1, svc.c_str(), buf, ilen, call_flags);
  if (intx) {
    fux::tx_detach();
  }
  if (rcd == -1) {
    tpfree(buf);
    refuse(tperrno);
    return;
  }
  tpfree(buf);
  if (!(call_flags & TPNOREPLY)) {
    l.pending.insert(rcd);
  }
}

// The connection's transaction agent: begin creates a domain
// transaction on the client's behalf, commit drives the full two-phase
// sequence, abort rolls back. Replies reuse the txctl frame:
// u32 rval, then the dotted xid for begin.
void handle_txctl(int fd, fux::link::frame &f) {
  auto it = links.find(fd);
  if (it == links.end() || it->second.cl == nullptr) {
    eng.cut(fd);
    return;
  }
  auto &l = it->second;
  fux::wire::reader r(f.payload);
  uint16_t op;
  uint32_t timeout;
  std::string xids;
  if (!r.u16(op) || !r.u32(timeout) || !r.str(xids)) {
    eng.cut(fd);
    return;
  }
  (void)timeout;

  auto ack = [&](uint32_t rval, const std::string &xid) {
    std::vector<char> payload;
    fux::wire::writer w(payload);
    w.u32(rval);
    w.str(xid);
    send_frame(fd, fux::link::txctl, std::move(payload));
  };

  if (op == kTxBegin) {
    if (!l.tx.empty()) {
      ack(TPEPROTO, "");
      return;
    }
    // the client runs no XA: this process originates the transaction
    // and remembers it per connection
    XID xid = {};
    xid.formatID = 1;
    auto id = getmib().genuid();
    xid.gtrid_length = sizeof(id);
    xid.bqual_length = 1;
    std::copy_n(reinterpret_cast<char *>(&id), sizeof(id), xid.data);
    xid.data[xid.gtrid_length] = 0;
    try {
      getmib().transactions().start(xid);
    } catch (const std::exception &e) {
      userlog("WSL: cannot start transaction: %s", e.what());
      ack(TPESYSTEM, "");
      return;
    }
    l.tx = fux::to_string(&xid);
    l.tx_timeout = timeout;
    l.tx_started = steady::now();
    ack(0, l.tx);
    return;
  }

  if (xids != l.tx) {
    ack(TPEPROTO, "");
    return;
  }
  XID xid;
  try {
    xid = fux::to_xid(xids);
  } catch (const std::exception &) {
    l.tx.clear();
    ack(TPEPROTO, "");
    return;
  }

  if (op == kTxCommit) {
    bool committed = false;
    try {
      committed = fux::tm::drive(&xid);
    } catch (const std::exception &e) {
      userlog("WSL: transaction drive failed: %s", e.what());
    }
    try {
      auto &tbl = getmib().transactions();
      tbl.end(tbl.find(xid));
    } catch (const std::exception &) {
    }
    l.tx.clear();
    l.tx_timeout = 0;
    ack(committed ? 0 : TPEABORT, "");
    return;
  }
  if (op == kTxAbort) {
    l.tx_timeout = 0;
    try {
      fux::tm::coordinate(&xid, fux::tm::rollback);
    } catch (const std::exception &e) {
      userlog("WSL: rollback failed: %s", e.what());
    }
    try {
      auto &tbl = getmib().transactions();
      tbl.end(tbl.find(xid));
    } catch (const std::exception &) {
    }
    l.tx.clear();
    ack(0, "");
    return;
  }
  ack(TPEINVAL, "");
}

void on_frame(int fd, fux::link::frame f) {
  switch (f.type) {
    case fux::link::login:
      handle_login(fd, f);
      break;
    case fux::link::request:
      handle_request(fd, f);
      break;
    case fux::link::txctl:
      handle_txctl(fd, f);
      break;
    default:
      break;
  }
}

// Push whatever replies have arrived back over the links
void drain_replies() {
  for (auto &[fd, l] : links) {
    if (l.cl == nullptr || l.pending.empty()) {
      continue;
    }
    while (!l.pending.empty()) {
      char *buf = tpalloc(const_cast<char *>("CARRAY"), nullptr, 1024);
      if (buf == nullptr) {
        break;
      }
      int cd = 0;
      long len = 0;
      if (l.cl->tpgetrply(&cd, &buf, &len, TPNOBLOCK | TPGETANY) == -1) {
        tpfree(buf);
        break;  // nothing ready right now
      }
      l.pending.erase(cd);

      long rcode = tpurcode;
      long rval = TPMINVAL;
      std::vector<char> wire;
      if (!export_buf(buf, len, wire)) {
        rval = TPESYSTEM;
        wire = dummy_export();
      }
      tpfree(buf);

      std::vector<char> reply;
      fux::wire::writer w(reply);
      w.u32(static_cast<uint32_t>(cd));
      w.u32(static_cast<uint32_t>(rval));
      w.u64(static_cast<uint64_t>(rcode));
      w.bytes(wire.data(), wire.size());
      send_frame(fd, fux::link::reply, std::move(reply));
    }
  }
}

void reap_expired_tx() {
  for (auto &kv : links) {
    auto &l = kv.second;
    if (l.tx.empty() || l.tx_timeout == 0) {
      continue;
    }
    if (now - l.tx_started < std::chrono::seconds(l.tx_timeout)) {
      continue;
    }
    userlog("WSL: transaction %s timed out after %us, rolling back",
            l.tx.c_str(), l.tx_timeout);
    rollback_tx(l);
  }
}

void idle_hook() {
  now = steady::now();
  reap_expired_tx();
  drain_replies();
  timeval tv{0, 0};
  eng.poll(&tv);
  now = steady::now();
  drain_replies();
}

bool split_nwaddr(const std::string &nwaddr, std::string &host, uint16_t &port) {
  auto s = nwaddr;
  if (s.compare(0, 2, "//") == 0) {
    s = s.substr(2);
  }
  auto colon = s.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  host = s.substr(0, colon);
  auto p = std::stoul(s.substr(colon + 1));
  if (p == 0 || p > 65535) {
    return false;
  }
  port = static_cast<uint16_t>(p);
  return true;
}

}  // namespace

int tpsvrinit(int argc, char *argv[]) {
  std::string naddr;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      naddr = argv[++i];
    } else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
      max_clients = std::stoul(argv[++i]);
    }
  }
  if (naddr.empty()) {
    userlog("WSL: no listener address, use CLOPT=\"-n //host:port --\"");
    return -1;
  }
  if (max_clients == 0) {
    // fall back to the machine entry limit
    max_clients = static_cast<size_t>(getmib().mach().maxwsclients);
  }
  std::string host;
  uint16_t port = 0;
  if (!split_nwaddr(naddr, host, port)) {
    userlog("WSL: cannot parse listener address %s", naddr.c_str());
    return -1;
  }

  eng.on_frame = on_frame;
  eng.on_accept = on_accept;
  eng.on_close = on_close;

  auto &m = getmib();
  std::string appdir = m.mach().appdir;
  if (appdir.empty()) {
    appdir = ".";
  }
  if (fux::util::getenv("FUXDO_LINK_PLAIN", "") != "1") {
    try {
      sec = std::make_unique<fux::link::security>(appdir + "/.fuxkeys",
                                                  "wsl-" +
                                                      std::to_string(port));
    } catch (const std::exception &e) {
      userlog("WSL: cannot set up link security: %s", e.what());
      return -1;
    }
    eng.secure(sec.get());
    eng.on_verify = on_verify_bridge;
  }

  if (eng.listen(host, port) == -1) {
    userlog("WSL: cannot listen on %s", naddr.c_str());
    return -1;
  }
  userlog("WSL: listening on %s", naddr.c_str());

  // idle drive: a periodic timer interrupts the dispatch receive so
  // socket events and queued replies keep flowing
  struct sigaction sa = {};
  sa.sa_handler = +[](int) {};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, nullptr);
  struct itimerval it = {};
  it.it_interval = timeval{0, 50000};
  it.it_value = timeval{0, 50000};
  setitimer(ITIMER_REAL, &it, nullptr);
  fux::poll_hook = idle_hook;
  return 0;
}

void tpsvrdone() {
  fux::poll_hook = nullptr;
  struct itimerval it = {};
  setitimer(ITIMER_REAL, &it, nullptr);
}

static struct tmdsptchtbl_t _tmdsptchtbl[] = {{NULL, NULL, NULL, 0, 0, NULL}};
static struct tmsvrargs_t tmsvrargs = {
    NULL, _tmdsptchtbl, 0,    tpsvrinit, tpsvrdone, _tmrunserver,
    NULL, NULL,         NULL, NULL,      NULL,      NULL,
    NULL, NULL};

struct tmsvrargs_t *_tmgetsvrargs() {
  return &tmsvrargs;
}

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
