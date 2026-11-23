// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// GWTDOMAIN server: the domain gateway for one local access point, a
// fuxedo-provided binary booted in a gateway group like an application
// server:
//
//   GWTDOMAIN SRVGRP=GW1 SRVID=10 CLOPT="--"
//
// The group name must match the GWGRP of a *DM_LOCAL_DOMAINS entry in the
// BDMCONFIG named by the BDMCONFIG environment variable. The gateway
// listens on the local access point's DM_TDOMAIN NWADDR, dials remotes
// per CONNECTION_POLICY, advertises this side's DM_IMPORT services into
// the local bulletin board, and forwards requests and replies across the
// link.
//
// Single threaded by design: the socket engine runs on the dispatch
// thread. While a forwarded call waits for its remote reply the service
// routine itself drives the engine poll loop; while idle, a periodic
// timer interrupts the dispatch receive and the installed poll hook
// sweeps the sockets.

#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>

#include <atmi.h>
#include <fml32.h>
#include <tpadm.h>
#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "dmcfg.h"
#include "dmib.h"
#include "dmlink.h"
#include "tlog.h"
#include "wire.h"
#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "probes.h"
#include "trx.h"
#include "servermain.h"

namespace {

using steady = std::chrono::steady_clock;

struct remote_ap {
  std::string name;     // access point name in *DM_REMOTE_DOMAINS
  std::string apid;     // its ACCESSPOINTID
  std::string host;
  uint16_t port = 0;
  std::string policy;   // effective CONNECTION_POLICY
  steady::time_point next_retry{};
};

struct link {
  std::string apid;     // peer identity, empty until its hello arrives
  std::string want;     // apid we dialed for, empty for accepted links
  std::string fingerprint;  // peer certificate fingerprint
  bool dialing = false;
  bool greeted = false;
  steady::time_point last_io{};
  std::vector<std::string> services;  // as announced by the peer
};

fux::link::engine eng;
std::unique_ptr<fux::link::security> sec;
fux::cfg::dmconfig dm;
std::string lap;
std::string lap_id;
// The Domains transaction log for this access point: a named log inside
// the crdl-created DMTLOGDEV device, auto-created at boot the way GWADM
// does. Empty when the access point has no DMTLOGDEV - such a gateway
// group must refuse transactional requests.
std::optional<fux::tlog::log> dmtlog;
std::vector<remote_ap> remotes;
std::map<int, link> links;
long blocktime_ms = 6000;
steady::time_point now;

uint32_t next_corrid = 1;
uint32_t pending_corrid = 0;
int pending_fd = -1;
std::optional<fux::link::frame> pending_frame;

// in-flight remote branches: transaction id string -> peer access point
// id, so the group's ".TM" calls can be forwarded to the right link
std::map<std::string, std::string> remote_branches;
// boot-time recovery queries awaiting the peer's decision
std::set<uint32_t> pending_queries;

void sweep();

std::vector<std::string> exported_services() {
  std::vector<std::string> out;
  for (auto &e : dm.exports) {
    if (e.laccesspoint == lap) {
      out.push_back(e.servicename);
    }
  }
  return out;
}

const fux::cfg::dm_import_entry *find_import(const char *svc) {
  for (auto &i : dm.imports) {
    if (i.servicename == svc && i.laccesspoint == lap) {
      return &i;
    }
  }
  return nullptr;
}

const remote_ap *find_remote(const std::string &name) {
  for (auto &r : remotes) {
    if (r.name == name) {
      return &r;
    }
  }
  return nullptr;
}

int fd_by_apid(const std::string &apid) {
  for (auto &[fd, l] : links) {
    if (!l.apid.empty() && l.apid == apid) {
      return fd;
    }
  }
  return -1;
}

bool apid_in_flight(const std::string &apid) {
  for (auto &[fd, l] : links) {
    if (l.dialing && l.want == apid) {
      return true;
    }
  }
  return false;
}

std::string current_gtrid() {
  if (!fux::tx::transactional()) {
    return {};
  }
  auto xid = fux::tx::current_xid();
  return fux::to_string(&xid);
}

// Enlist this gateway group in the caller's transaction and remember
// which link carries the branch, so the ".TM" endpoint can reach it.
void join_branch(const std::string &apid) {
  auto xid = fux::tx::current_xid();
  auto &tbl = getmib().transactions();
  auto g = tbl.find(xid);
  tbl.join(g, fux::tx::grpidx);
  remote_branches[fux::to_string(&xid)] = apid;
}

// The machine TLOG holds this domain's own decisions; a peer asks about
// them with a query when it reboots with an in-doubt branch. Answer with
// the tx operation that resolves the branch, or 0 while unknown.
uint32_t query_outcome(const XID &xid) {
  auto *device = getmib().mach().tlogdevice;
  if (device[0] == '\0') {
    return 0;
  }
  try {
    auto log = fux::tlog::log::open(device);
    switch (log.outcome(&xid)) {
      case 1:
        return fux::tm::commit;
      case 2:
      case 3:
        return fux::tm::rollback;
      default:
        return 0;
    }
  } catch (const std::exception &) {
    return 0;
  }
}

// Exports a typed buffer into wire bytes. The export always carries at
// least the type header, so the far side's import never sees a zero-byte
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

void queue_reply(int fd, uint32_t corrid, long rval, long rcode,
                 std::vector<char> payload) {
  fux::link::frame f{fux::link::reply, {}};
  fux::wire::writer w(f.payload);
  w.u32(corrid);
  w.u32(static_cast<uint32_t>(rval));
  w.u64(static_cast<uint64_t>(rcode));
  w.bytes(payload.data(), payload.size());
  eng.queue(fd, std::move(f));
}

void greet(int fd) {
  auto &l = links[fd];
  if (l.greeted) {
    return;
  }
  {
    fux::link::frame f{fux::link::hello, {}};
    fux::wire::writer w(f.payload);
    w.str(lap_id);
    eng.queue(fd, std::move(f));
  }
  {
    fux::link::frame f{fux::link::services, {}};
    fux::wire::writer w(f.payload);
    auto svcs = exported_services();
    w.u16(static_cast<uint16_t>(svcs.size()));
    for (auto &s : svcs) {
      w.str(s);
    }
    eng.queue(fd, std::move(f));
  }
  l.greeted = true;
  l.last_io = now;
}

// Trust-on-first-use: outbound links check the peer's fingerprint
// against the pin cache when one exists; accepted links are pinned
// under the peer's name once its hello reveals it.
bool on_verify(int fd, const std::string &fp, bool dialed) {
  if (sec == nullptr) {
    return false;
  }
  // the verify callback runs before the accept callback, so accepted
  // links have no entry yet; create it
  auto &l = links[fd];
  l.fingerprint = fp;
  if (dialed && !l.want.empty()) {
    std::string pinned;
    if (sec->pin_load(l.want, pinned) && pinned != fp) {
      userlog("GWTDOMAIN: fingerprint mismatch for %s, link refused",
              l.want.c_str());
      return false;
    }
  }
  return true;
}

void dial(remote_ap &r) {
  int fd = eng.connect(r.host, r.port);
  if (fd == -1) {
    r.next_retry = now + std::chrono::seconds(30);
    return;
  }
  auto &l = links[fd];
  l.want = r.apid;
  l.dialing = true;
  l.last_io = now;
  r.next_retry = steady::time_point::max();
  greet(fd);
}

void on_accept(int fd) {
  links[fd].last_io = now;
  greet(fd);
}

void on_close(int fd) {
  auto it = links.find(fd);
  if (it == links.end()) {
    return;
  }
  userlog("GWTDOMAIN: link on fd %d closed, peer %s", fd,
          it->second.apid.empty() ? "unknown" : it->second.apid.c_str());
  bool was_pending = (fd == pending_fd && pending_corrid != 0);
  links.erase(it);
  if (was_pending) {
    fux::link::frame f{fux::link::reply, {}};
    fux::wire::writer w(f.payload);
    w.u32(pending_corrid);
    w.u32(TPESYSTEM);
    w.u64(0);
    auto payload = dummy_export();
    w.bytes(payload.data(), payload.size());
    pending_frame = std::move(f);
  }
  for (auto &r : remotes) {
    if (r.policy == "ON_STARTUP" && fd_by_apid(r.apid) == -1 &&
        !apid_in_flight(r.apid)) {
      r.next_retry = now + std::chrono::seconds(30);
    }
  }
}

void handle_request(int fd, fux::link::frame &f) {
  fux::wire::reader r(f.payload);
  std::string name;
  uint32_t corrid, flags;
  std::vector<char> payload;
  if (!r.str(name) || !r.u32(corrid) || !r.u32(flags) || !r.bytes(payload) ||
      name.empty()) {
    userlog("GWTDOMAIN: malformed request frame, dropping link");
    eng.cut(fd);
    return;
  }
  // transactional requests carry the caller's transaction id after the
  // payload; absent on links that predate cross-domain transactions
  std::string gtrid;
  r.str(gtrid);
  links[fd].last_io = now;
  if ((flags & TPTRAN) && !dmtlog.has_value()) {
    // without a DMTLOG this gateway group may not process requests in
    // transaction mode
    userlog("GWTDOMAIN: transactional request for %s refused (no DMTLOGDEV "
            "for this access point)",
            name.c_str());
    queue_reply(fd, corrid, TPEABORT, 0, dummy_export());
    return;
  }
  char *buf = tpalloc(const_cast<char *>("CARRAY"), nullptr,
                      static_cast<long>(payload.size()) + 1024);
  if (buf == nullptr) {
    queue_reply(fd, corrid, TPESYSTEM, 0, dummy_export());
    return;
  }
  long olen = 0;
  if (tpimport(payload.data(), static_cast<long>(payload.size()), &buf, &olen,
               0) == -1) {
    tpfree(buf);
    queue_reply(fd, corrid, TPESYSTEM, 0, dummy_export());
    return;
  }
  // tpimport reports the exported size; the XATMI calls take the
  // logical data length
  long ilen = olen - fux::mem::headersize();
  bool intx = (flags & TPTRAN) != 0;
  if (intx) {
    try {
      fux::tx_adopt(fux::to_xid(gtrid));
    } catch (const std::exception &e) {
      userlog("GWTDOMAIN: cannot adopt transaction for %s: %s", name.c_str(),
              e.what());
      tpfree(buf);
      queue_reply(fd, corrid, TPEABORT, 0, dummy_export());
      return;
    }
  }
  int rc = ::tpcall(const_cast<char *>(name.c_str()), buf, ilen, &buf, &olen,
                    intx ? 0 : TPNOTRAN);
  if (intx) {
    fux::tx_detach();
  }
  long rval = TPMINVAL;
  long rcode = 0;
  std::vector<char> wire;
  if (rc == 0) {
    rcode = tpurcode;
    if (!export_buf(buf, olen, wire)) {
      rval = TPESYSTEM;
      wire = dummy_export();
    }
  } else {
    rval = tperrno;
    if (!export_buf(buf, olen, wire)) {
      wire = dummy_export();
    }
  }
  tpfree(buf);
  queue_reply(fd, corrid, rval, rcode, std::move(wire));
}

// Drive a 2PC control frame arriving on a link: prepare/commit/rollback
// coordinate the local branch set, query answers from the machine TLOG.
// The log records carry the raw gtrid bytes; the dotted wire form
// reconstructs the rest (outcome matching uses the gtrid only).
std::string dotted_gtrid(const std::string &raw) {
  static const char *digits = "0123456789abcdef";
  std::string out = "0.";
  for (auto c : raw) {
    out += digits[static_cast<unsigned char>(c) >> 4];
    out += digits[static_cast<unsigned char>(c) & 0xf];
  }
  return out;
}

void handle_txnctrl(int fd, uint32_t corrid, uint16_t op,
                    const std::string &xids) {
  auto ack = [&](uint32_t result) {
    fux::link::frame a{fux::link::txnctrl, {}};
    fux::wire::writer w(a.payload);
    w.u32(corrid);
    w.u32(result);
    w.str(xids);
    eng.queue(fd, std::move(a));
  };
  XID xid;
  try {
    xid = fux::to_xid(xids);
  } catch (const std::exception &) {
    ack(XAER_INVAL);
    return;
  }
  switch (op) {
    case fux::tm::prepare: {
      if (!dmtlog.has_value()) {
        ack(XAER_RMERR);
        return;
      }
      try {
        fux::tx_adopt(xid);
      } catch (const std::exception &) {
        // already tracked; the context association below still applies
      }
      int rc;
      try {
        rc = fux::tm::coordinate(&xid, fux::tm::prepare);
      } catch (const std::out_of_range &) {
        ack(XAER_NOTA);
        return;
      } catch (const std::exception &e) {
        userlog("GWTDOMAIN: local prepare failed: %s", e.what());
        ack(XAER_RMERR);
        return;
      }
      fux::tx_detach();
      try {
        // a read-only branch is decision-independent: record it
        // terminal so recovery never re-drives it
        dmtlog->append(rc == XA_RDONLY ? fux::tlog::record_type::heuristic
                                       : fux::tlog::record_type::prepared,
                       &xid);
      } catch (const std::exception &e) {
        userlog("GWTDOMAIN: DMTLOG append failed: %s", e.what());
      }
      ack(rc);
      return;
    }
    case fux::tm::commit:
    case fux::tm::rollback: {
      if (!dmtlog.has_value()) {
        ack(XAER_RMERR);
        return;
      }
      try {
        fux::tm::coordinate(&xid, static_cast<int>(op));
      } catch (const std::out_of_range &) {
        // the branch is no longer tracked here (already resolved or
        // this domain rebooted): the decision is still safe to record
      } catch (const std::exception &e) {
        userlog("GWTDOMAIN: local %s failed: %s",
                op == fux::tm::commit ? "commit" : "rollback", e.what());
      }
      try {
        dmtlog->append(op == fux::tm::commit ? fux::tlog::record_type::commit
                                             : fux::tlog::record_type::rollback,
                       &xid);
      } catch (const std::exception &e) {
        userlog("GWTDOMAIN: DMTLOG append failed: %s", e.what());
      }
      try {
        auto &tbl = getmib().transactions();
        tbl.end(tbl.find(xid));
      } catch (const std::exception &) {
      }
      ack(XA_OK);
      return;
    }
    default:
      ack(query_outcome(xid));
      return;
  }
}

// Apply a recovery decision answered by the peer for one of our
// in-doubt branches.
void apply_recovery(const std::string &xids, uint32_t op) {
  try {
    XID xid = fux::to_xid(xids);
    if (op == fux::tm::commit || op == fux::tm::rollback) {
      userlog("GWTDOMAIN: recovery resolved %s with %s", xids.c_str(),
              op == fux::tm::commit ? "COMMIT" : "ROLLBACK");
      // drive whatever is still tracked locally, then record the
      // terminal outcome; after a reboot the branch may be gone, which
      // is fine - the record is what matters
      try {
        fux::tm::coordinate(&xid, static_cast<int>(op));
      } catch (const std::exception &) {
      }
      if (dmtlog.has_value()) {
        dmtlog->append(op == fux::tm::commit ? fux::tlog::record_type::commit
                                             : fux::tlog::record_type::rollback,
                       &xid);
      }
      try {
        auto &tbl = getmib().transactions();
        tbl.end(tbl.find(xid));
      } catch (const std::exception &) {
      }
    }
  } catch (const std::exception &e) {
    userlog("GWTDOMAIN: recovery apply failed: %s", e.what());
  }
}

// Queue a 2PC control frame (correlator allocated here; boot-time
// recovery queries remember it in pending_queries).
uint32_t queue_txnctrl(int fd, uint16_t op, const std::string &xids,
                       bool track) {
  FUX_PROBE2(domain__txnctrl, op, xids.c_str());
  uint32_t corrid = next_corrid++;
  if (corrid == 0) {
    corrid = next_corrid++;
  }
  fux::link::frame f{fux::link::txnctrl, {}};
  fux::wire::writer w(f.payload);
  w.u32(corrid);
  w.u16(op);
  w.str(xids);
  eng.queue(fd, std::move(f));
  if (track) {
    pending_queries.insert(corrid);
  }
  return corrid;
}

// The ".TM" endpoint of a gateway group: forwards the coordinator's
// prepare/commit/rollback for a remote branch over the link and waits
// for the peer's aggregate result.
void gwtm_service(TPSVCINFO *svcinfo) {
  fux::fml32buf rs;
  int ret = XAER_RMERR;
  try {
    fux::fml32buf rq(svcinfo);
    auto func = rq.get<long>(TA_XAFUNC, 0);
    auto xids = rq.get<std::string>(TA_XID, 0);
    FUX_PROBE2(domain__branch, func, xids.c_str());
    auto it = remote_branches.find(xids);
    if (it == remote_branches.end()) {
      ret = XAER_NOTA;
    } else {
      int fd = fd_by_apid(it->second);
      if (fd == -1) {
        ret = XAER_RMFAIL;
      } else {
        uint32_t corrid = queue_txnctrl(fd, static_cast<uint16_t>(func), xids, false);
        pending_corrid = corrid;
        pending_fd = fd;
        auto deadline = steady::now() + std::chrono::milliseconds(blocktime_ms);
        while (!pending_frame.has_value()) {
          now = steady::now();
          if (now >= deadline) {
            break;
          }
          sweep();
          timeval slice{0, 20000};
          eng.poll(&slice);
        }
        pending_corrid = 0;
        pending_fd = -1;
        if (!pending_frame.has_value()) {
          ret = XAER_RMFAIL;
        } else {
          fux::link::frame reply = std::move(*pending_frame);
          pending_frame.reset();
          fux::wire::reader r(reply.payload);
          uint32_t rcvd, result;
          if (!r.u32(rcvd) || rcvd != corrid || !r.u32(result)) {
            ret = XAER_RMERR;
          } else {
            ret = static_cast<int>(result);
          }
        }
      }
    }
  } catch (const std::exception &) {
    ret = XAER_RMERR;
  }
  FUX_PROBE1(domain__branch_return, ret);
  rs.put(TA_XARET, 0, ret);
  fux::tpreturn(TPSUCCESS, 0, rs);
}

void on_frame(int fd, fux::link::frame f) {
  auto it = links.find(fd);
  if (it == links.end()) {
    return;
  }
  it->second.last_io = now;
  switch (f.type) {
    case fux::link::hello: {
      fux::wire::reader r(f.payload);
      std::string apid;
      if (!r.str(apid)) {
        userlog("GWTDOMAIN: malformed hello, link dropped");
        eng.cut(fd);
        return;
      }
      it->second.apid = apid;
      it->second.dialing = false;
      if (sec != nullptr && !it->second.fingerprint.empty()) {
        std::string pinned;
        if (sec->pin_load(apid, pinned)) {
          if (pinned != it->second.fingerprint) {
            userlog("GWTDOMAIN: fingerprint mismatch for %s, link closed",
                    apid.c_str());
            eng.cut(fd);
            return;
          }
        } else {
          sec->pin_store(apid, it->second.fingerprint);
        }
      }
      userlog("GWTDOMAIN: link up with %s on fd %d", apid.c_str(), fd);
      // re-drive in-doubt subordinate branches: ask the peer what it
      // decided for every prepared vote that has no terminal record
      if (dmtlog.has_value()) {
        try {
          size_t outstanding = 0;
          for (auto &rec : dmtlog->recent()) {
            if (rec.second ==
                static_cast<int>(fux::tlog::record_type::prepared)) {
              queue_txnctrl(fd, 3, dotted_gtrid(rec.first), true);
              outstanding++;
            }
          }
          if (outstanding > 0) {
            userlog("GWTDOMAIN: %zu in-doubt transaction(s), querying %s",
                    outstanding, apid.c_str());
          }
        } catch (const std::exception &e) {
          userlog("GWTDOMAIN: DMTLOG scan failed: %s", e.what());
        }
      }
      break;
    }
    case fux::link::txnctrl: {
      fux::wire::reader r(f.payload);
      uint32_t corrid;
      if (!r.u32(corrid)) {
        break;
      }
      if (corrid != 0 && corrid == pending_corrid) {
        pending_frame = std::move(f);
        break;
      }
      auto q = pending_queries.find(corrid);
      if (q != pending_queries.end()) {
        pending_queries.erase(q);
        uint32_t result;
        std::string xids;
        fux::wire::reader ar(f.payload);
        ar.u32(corrid);
        if (ar.u32(result) && ar.str(xids)) {
          apply_recovery(xids, result);
        }
        break;
      }
      uint16_t op;
      std::string xids;
      if (!r.u16(op) || !r.str(xids)) {
        userlog("GWTDOMAIN: malformed txnctrl frame, dropping link");
        eng.cut(fd);
        return;
      }
      handle_txnctrl(fd, corrid, op, xids);
      break;
    }
    case fux::link::services: {
      fux::wire::reader r(f.payload);
      uint16_t count;
      if (!r.u16(count)) {
        break;
      }
      std::vector<std::string> svcs;
      for (uint16_t i = 0; i < count; i++) {
        std::string s;
        if (!r.str(s)) {
          return;
        }
        svcs.push_back(s);
      }
      it->second.services = std::move(svcs);
      break;
    }
    case fux::link::request:
      handle_request(fd, f);
      break;
    case fux::link::reply: {
      fux::wire::reader r(f.payload);
      uint32_t corrid;
      if (!r.u32(corrid)) {
        break;
      }
      if (corrid != 0 && corrid == pending_corrid) {
        pending_frame = std::move(f);
      }
      break;
    }
    default:
      break;
  }
}

// Outbound retries and heartbeat pacing; cheap enough to run after every
// poll sweep.
void sweep() {
  for (auto &r : remotes) {
    if (r.policy != "ON_STARTUP") {
      continue;
    }
    if (fd_by_apid(r.apid) != -1 || apid_in_flight(r.apid)) {
      continue;
    }
    if (now >= r.next_retry) {
      dial(r);
    }
  }
  for (auto &[fd, l] : links) {
    if (now - l.last_io > std::chrono::seconds(15)) {
      eng.queue(fd, {fux::link::heartbeat, {}});
      l.last_io = now;
    }
  }
}

void idle_hook() {
  now = steady::now();
  sweep();
  timeval tv{0, 0};
  eng.poll(&tv);
  now = steady::now();
  sweep();
}

// Service routine for the dmadmin endpoint (DMADM_<accesspoint>):
// connection state queries plus connect/disconnect requests.
void dmadmin_service(TPSVCINFO *svcinfo) {
  fux::fml32buf rq(svcinfo);
  fux::fml32buf rs;
  auto op = rq.get<std::string>(dmib::operation(), 0, "");
  if (op == "CONNECT" || op == "DISCONNECT") {
    auto ap = rq.get<std::string>(dmib::accesspoint(), 0, "");
    const remote_ap *r = find_remote(ap);
    if (r == nullptr) {
      fux::tpreturn(TPFAIL, 0, rs);
      return;
    }
    if (op == "CONNECT") {
      if (fd_by_apid(r->apid) == -1 && !apid_in_flight(r->apid)) {
        dial(const_cast<remote_ap &>(*r));
      }
    } else {
      // mutual dialing can leave more than one link to the same access
      // point: drop them all
      for (auto it = links.begin(); it != links.end();) {
        if (it->second.apid == r->apid) {
          eng.cut(it->first);
          it = links.begin();
        } else {
          ++it;
        }
      }
    }
  } else if (op != "GET") {
    fux::tpreturn(TPFAIL, 0, rs);
    return;
  }
  int oc = 0;
  for (auto &r : remotes) {
    std::string nw = "//" + r.host + ":" + std::to_string(r.port);
    rs.put(dmib::accesspoint(), oc, r.name);
    rs.put(dmib::accesspointid(), oc, r.apid);
    rs.put(dmib::nwaddr(), oc, nw);
    rs.put(dmib::policy(), oc, r.policy);
    rs.put(dmib::state(), oc,
           fd_by_apid(r.apid) != -1 ? "UP" : "DOWN");
    oc++;
  }
  fux::tpreturn(TPSUCCESS, 0, rs);
}

// Service routine for an imported name: forwards the call over the link
// and, while waiting, drives the engine so inbound frames (including the
// reply) keep flowing on this thread.
void forward_service(TPSVCINFO *svcinfo) {
  const auto *imp = find_import(svcinfo->name);
  if (imp == nullptr) {
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  const auto *rmt = find_remote(imp->raccesspoint);
  if (rmt == nullptr) {
    userlog("GWTDOMAIN: %s: remote access point %s not configured",
            svcinfo->name, imp->raccesspoint.c_str());
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  std::string gtrid;
  if (svcinfo->flags & TPTRAN) {
    if (!dmtlog.has_value()) {
      userlog("GWTDOMAIN: transactional call to %s refused (no DMTLOGDEV "
              "for this access point)",
              svcinfo->name);
      ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
      return;
    }
    try {
      join_branch(rmt->apid);
      gtrid = current_gtrid();
    } catch (const std::exception &e) {
      userlog("GWTDOMAIN: cannot enlist %s: %s", svcinfo->name, e.what());
      ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
      return;
    }
  }
  now = steady::now();
  auto deadline = now + std::chrono::milliseconds(blocktime_ms);

  int fd = fd_by_apid(rmt->apid);
  if (fd == -1 && (rmt->policy == "ON_STARTUP" || rmt->policy == "ON_DEMAND")) {
    if (!apid_in_flight(rmt->apid)) {
      dial(const_cast<remote_ap &>(*rmt));
    }
    while ((fd = fd_by_apid(rmt->apid)) == -1) {
      now = steady::now();
      if (now >= deadline) {
        break;
      }
      sweep();
      timeval slice{0, 20000};
      eng.poll(&slice);
    }
  }
  if (fd == -1) {
    userlog("GWTDOMAIN: no link to %s for %s", rmt->apid.c_str(),
            svcinfo->name);
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }

  std::vector<char> wire;
  if (!export_buf(svcinfo->data, svcinfo->len, wire)) {
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  uint32_t corrid = next_corrid++;
  if (corrid == 0) {
    corrid = next_corrid++;
  }
  {
    fux::link::frame f{fux::link::request, {}};
    fux::wire::writer w(f.payload);
    std::string rname = imp->rname.empty() ? imp->servicename : imp->rname;
    w.str(rname);
    w.u32(corrid);
    w.u32(gtrid.empty() ? 0 : TPTRAN);
    w.bytes(wire.data(), wire.size());
    w.str(gtrid);
    if (!eng.queue(fd, std::move(f))) {
      ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
      return;
    }
  }
  links[fd].last_io = now;
  pending_corrid = corrid;
  pending_fd = fd;
  while (!pending_frame.has_value()) {
    now = steady::now();
    if (now >= deadline) {
      break;
    }
    sweep();
    timeval slice{0, 20000};
    eng.poll(&slice);
  }
  pending_corrid = 0;
  pending_fd = -1;
  if (!pending_frame.has_value()) {
    userlog("GWTDOMAIN: reply for %s timed out", svcinfo->name);
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  fux::link::frame reply = std::move(*pending_frame);
  pending_frame.reset();
  fux::wire::reader r(reply.payload);
  uint32_t rcvd, rval;
  uint64_t rcode;
  std::vector<char> payload;
  if (!r.u32(rcvd) || rcvd != corrid || !r.u32(rval) || !r.u64(rcode) ||
      !r.bytes(payload)) {
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  if (rval != TPMINVAL) {
    ::tpreturn(TPFAIL, static_cast<long>(rcode), svcinfo->data, 0, 0);
    return;
  }
  char *buf = tpalloc(const_cast<char *>("CARRAY"), nullptr,
                      static_cast<long>(payload.size()) + 1024);
  if (buf == nullptr) {
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  long olen = 0;
  if (tpimport(payload.data(), static_cast<long>(payload.size()), &buf, &olen,
               0) == -1) {
    tpfree(buf);
    ::tpreturn(TPFAIL, 0, svcinfo->data, 0, 0);
    return;
  }
  ::tpreturn(TPSUCCESS, static_cast<long>(rcode), buf,
             olen - fux::mem::headersize(), 0);
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
  (void)argc;
  (void)argv;
  auto path = fux::util::getenv("BDMCONFIG", "");
  if (path.empty()) {
    userlog("GWTDOMAIN: BDMCONFIG environment variable not set");
    return -1;
  }
  try {
    dm = fux::cfg::dm_load_file(path);
  } catch (const std::exception &e) {
    userlog("GWTDOMAIN: %s", e.what());
    return -1;
  }

  auto &m = getmib();
  std::string srvgrp;
  try {
    srvgrp = m.group_by_id(fux::tx::grpno).srvgrp;
  } catch (const std::exception &) {
    userlog("GWTDOMAIN: group %d not in bulletin board", fux::tx::grpno);
    return -1;
  }
  const fux::cfg::dm_local_domain *local = nullptr;
  for (auto &l : dm.locals) {
    if (l.gwgrp == srvgrp) {
      local = &l;
    }
  }
  if (local == nullptr) {
    userlog("GWTDOMAIN: group %s is not a gateway group (no matching GWGRP "
            "in BDMCONFIG)",
            srvgrp.c_str());
    return -1;
  }
  lap = local->accesspoint;
  lap_id = local->accesspointid;
  blocktime_ms = local->blocktime * 1000;

  if (!local->dmtlogdev.empty()) {
    try {
      dmtlog = fux::tlog::log::open_or_create(
          local->dmtlogdev + "/" + local->dmtlogname, local->dmtlogsize);
      userlog("GWTDOMAIN: domain transaction log %s open in %s "
              "(%zu outstanding record(s))",
              local->dmtlogname.c_str(), local->dmtlogdev.c_str(),
              dmtlog->recent().size());
    } catch (const std::exception &e) {
      userlog("GWTDOMAIN: cannot open domain transaction log %s in %s: %s",
              local->dmtlogname.c_str(), local->dmtlogdev.c_str(), e.what());
      return -1;
    }
  }

  const fux::cfg::dm_tdomain_addr *mine = nullptr;
  for (auto &t : dm.tdomain) {
    if (t.accesspoint == lap) {
      mine = &t;
    }
  }
  if (mine == nullptr) {
    userlog("GWTDOMAIN: local access point %s has no DM_TDOMAIN NWADDR",
            lap.c_str());
    return -1;
  }
  std::string host;
  uint16_t port = 0;
  if (!split_nwaddr(mine->nwaddr, host, port)) {
    userlog("GWTDOMAIN: cannot parse NWADDR %s", mine->nwaddr.c_str());
    return -1;
  }

  for (auto &rd : dm.remotes) {
    remote_ap r;
    r.name = rd.accesspoint;
    r.apid = rd.accesspointid;
    const fux::cfg::dm_tdomain_addr *ta = nullptr;
    for (auto &t : dm.tdomain) {
      if (t.accesspoint == rd.accesspoint) {
        ta = &t;
      }
    }
    if (ta == nullptr) {
      userlog("GWTDOMAIN: remote %s has no DM_TDOMAIN entry, skipping",
              rd.accesspoint.c_str());
      continue;
    }
    if (!split_nwaddr(ta->nwaddr, r.host, r.port)) {
      userlog("GWTDOMAIN: cannot parse remote NWADDR %s, skipping",
              ta->nwaddr.c_str());
      continue;
    }
    r.policy = ta->connection_policy.empty() ? local->connection_policy
                                             : ta->connection_policy;
    remotes.push_back(std::move(r));
  }

  eng.on_frame = on_frame;
  eng.on_accept = on_accept;
  eng.on_close = on_close;

  // Links are TLS with trust-on-first-use pins by default; the identity
  // key and the pin cache live in a private directory under the
  // application directory. FUXDO_LINK_PLAIN=1 opts out (debugging only).
  if (fux::util::getenv("FUXDO_LINK_PLAIN", "") != "1") {
    std::string appdir = m.mach().appdir;
    if (appdir.empty()) {
      appdir = ".";
    }
    try {
      sec = std::make_unique<fux::link::security>(appdir + "/.fuxkeys",
                                                  lap_id);
    } catch (const std::exception &e) {
      userlog("GWTDOMAIN: cannot set up link security: %s", e.what());
      return -1;
    }
    eng.secure(sec.get());
    eng.on_verify = on_verify;
  }

  if (eng.listen(host, port) == -1) {
    userlog("GWTDOMAIN: cannot listen on %s", mine->nwaddr.c_str());
    return -1;
  }
  userlog("GWTDOMAIN: access point %s (%s) listening on %s", lap.c_str(),
          lap_id.c_str(), mine->nwaddr.c_str());

  now = steady::now();
  for (auto &r : remotes) {
    if (r.policy == "ON_STARTUP") {
      r.next_retry = now;
    }
  }

  for (auto &i : dm.imports) {
    if (i.laccesspoint != lap) {
      continue;
    }
    if (tpadvertise(const_cast<char *>(i.servicename.c_str()),
                    forward_service) == -1) {
      userlog("GWTDOMAIN: failed to advertise %s: %s", i.servicename.c_str(),
              tpstrerror(tperrno));
      return -1;
    }
  }
  std::string adm = "DMADM_" + lap;
  if (tpadvertise(const_cast<char *>(adm.c_str()), dmadmin_service) == -1) {
    userlog("GWTDOMAIN: failed to advertise %s: %s", adm.c_str(),
            tpstrerror(tperrno));
    return -1;
  }
  // the transaction endpoint of a gateway group: the local transaction
  // manager drives it like any group ".TM", the gateway forwards the
  // branch operations to the peer domain
  if (dmtlog.has_value()) {
    if (tpadvertise(const_cast<char *>(".TM"), gwtm_service) == -1) {
      userlog("GWTDOMAIN: failed to advertise .TM: %s",
              tpstrerror(tperrno));
      return -1;
    }
  }

  // Idle socket sweeps: a periodic real-time timer interrupts the
  // dispatch receive; without SA_RESTART the receive returns EINTR and
  // the hook above runs the engine. Client-side waits retry EINTR
  // internally, so local calls are not disturbed.
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
static struct tmsvrargs_t tmsvrargs =
    fuxedo_server_args(nullptr, _tmdsptchtbl, false);
struct tmsvrargs_t *_tmgetsvrargs() { return &tmsvrargs; }

int main(int argc, char **argv) {
  _tmbuilt_with_thread_option = 0;
  return _tmstartserver(argc, argv, _tmgetsvrargs());
}
