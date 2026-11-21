// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

#include <fml32.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "compat.h"
#include "ipc.h"
#include "mib.h"
#include "misc.h"
#include "routing.h"
#include "svcrepo.h"
#include "trace.h"
#include "trx.h"

#include "resp.h"
#include "svcrepo.h"
#include "trace.h"

#include <thread>

#include "client.h"
#include "probes.h"
#include "ws.h"
#include "xml.h"

namespace fux {
bool is_server();

struct suspend_guard {
  suspend_guard(bool suspend = true) : suspended(false) {
    if (suspend && fux::tx::transactional()) {
      suspended = true;
      fux::tx_suspend();
    }
  }
  ~suspend_guard() {
    if (suspended) {
      fux::tx_resume();
    }
  }
  bool suspended;
};

}  // namespace fux

client::client(mib &mibcon, const char *cltname, const char *usrname)
    : mibcon_(mibcon), repo_(mibcon), blocktime_next(0), blocktime_all(0) {
  auto lock = mibcon_.data_lock();
  client_ = mibcon_.make_accesser(getpid(), cltname, usrname);
  rpid = mibcon_.accessers().at(client_).rpid = fux::ipc::qcreate();
}

client::~client() {
  // Best-effort unregister: at process exit the application may
  // already be shut down and the bulletin board IPC gone; a throwing
  // destructor here would terminate the whole process.
  try {
    disconnect_convs();
    auto lock = mibcon_.data_lock();
    fux::ipc::qdelete(mibcon_.accessers().at(client_).rpid);
    mibcon_.accessers().at(client_).invalidate();
  } catch (const std::exception &) {
  }
}

int client::routed_group(const char *svc, char *data) {
  auto sidx = mibcon_.find_service(svc);
  if (sidx == mib::badoff) {
    return -1;
  }
  auto &svc_rec = mibcon_.services().at(sidx);
  if (svc_rec.routingname[0] == '\0') {
    return -1;
  }
  char type[16] = {};
  char subtype[16] = {};
  if (tptypes(data, type, subtype) == -1) {
    return -1;
  }
  auto rt = mibcon_.routings();
  for (size_t i = 0; i < rt->len; i++) {
    auto &e = rt.at(i);
    if (strcmp(e.name, svc_rec.routingname) != 0 ||
        !fux::routing::buftype_matches(e.buftype, type, subtype)) {
      continue;
    }
    if (strcmp(type, "FML32") != 0) {
      return -1;  // only FML32 buffers carry routing fields for now
    }
    auto fb = reinterpret_cast<FBFR32 *>(data);
    std::string value;
    fux::routing::field_kind kind = fux::routing::field_kind::string;
    if (e.fldtype == FLD_SHORT || e.fldtype == FLD_LONG) {
      long v = 0;
      if (Fget32(fb, static_cast<FLDID32>(e.fieldid), 0, reinterpret_cast<char *>(&v), 0) == -1) {
        TPERROR(TPESYSTEM, "Routing field %s null", e.field);
        return -2;
      }
      value = std::to_string(v);
      kind = fux::routing::field_kind::integer;
    } else if (e.fldtype == FLD_FLOAT || e.fldtype == FLD_DOUBLE) {
      double v = 0;
      if (Fget32(fb, static_cast<FLDID32>(e.fieldid), 0, reinterpret_cast<char *>(&v), 0) == -1) {
        TPERROR(TPESYSTEM, "Routing field %s null", e.field);
        return -2;
      }
      value = std::to_string(v);
      kind = fux::routing::field_kind::floating;
    } else if (e.fldtype == FLD_STRING || e.fldtype == FLD_CHAR) {
      char buf[256] = {};
      FLDLEN32 len = sizeof(buf);
      if (Fget32(fb, static_cast<FLDID32>(e.fieldid), 0, buf, &len) == -1) {
        TPERROR(TPESYSTEM, "Routing field %s null", e.field);
        return -2;
      }
      value.assign(buf, strnlen(buf, sizeof(buf)));
    } else {
      TPERROR(TPESYSTEM, "Routing field %s has unsupported type", e.field);
      return -2;
    }
    auto ranges = fux::routing::parse_ranges(e.ranges);
    auto *hit = fux::routing::match(ranges, value, kind);
    if (hit == nullptr) {
      TPERROR(TPESYSTEM, "Routing field value %s not valid", e.field);
      return -2;
    }
    int found = -1;
    for (auto g = hit->groups.rbegin(); g != hit->groups.rend(); ++g) {
      if (*g == "*") {
        return -1;  // any group
      }
      auto gidx = mibcon_.group_index(*g);
      if (gidx == mib::badoff) {
        continue;
      }
      if (found == -1) {
        found = gidx;
      }
      // prefer a listed group whose servers actually offer the service
      // right now (a not-yet-booted rollout target must fall through)
      auto adv = mibcon_.advertisements();
      for (size_t i = 0; i < adv->len; i++) {
        auto &a = adv.at(i);
        if (a.service == sidx && a.state != state_t::INValid &&
            mibcon_.servers().at(a.server).group_idx == gidx) {
          return mibcon_.groups().at(gidx).grpno;
        }
      }
    }
    if (found != -1) {
      return mibcon_.groups().at(found).grpno;
    }
    TPERROR(TPENOENT, "Routing group not found");
    return -2;
  }
  return -1;
}

int client::tpacall_group(int grpno, const char *svc, char *data, long len,
                          long flags) try {
  if (grpno == -1 && data != nullptr) {
    int routed = routed_group(svc, data);
    if (routed == -2) {
      return -1;
    }
    grpno = routed;
  }
  auto target = repo_.get_queue(grpno, svc);
  auto conv = repo_.buftypeconv(svc);

  // the platform forbids no-reply calls inside a transaction
  if ((flags & TPNOREPLY) && fux::tx::transactional()) {
    TPERROR(TPEPROTO, "TPNOREPLY in transaction mode");
    return -1;
  }
  // a group without a transaction manager cannot join a transaction:
  // reject with TPETRAN unless the caller opts out with TPNOTRAN.
  // Exception: a domain gateway group has no RM of its own - the
  // transaction passes through to the peer's resource managers, so
  // services imported through GWTDOMAIN stay callable in a transaction.
  if (!(flags & TPNOTRAN) && fux::tx::transactional() && target.grpno > 0) {
    auto gidx = mibcon_.find_group(static_cast<uint16_t>(target.grpno));
    if (gidx != mib::badoff &&
        mibcon_.groups().at(gidx).tmsname[0] == '\0' &&
        !mibcon_.is_gateway_group(gidx)) {
      TPERROR(TPETRAN,
              "service %s is in a group without a transaction manager",
              svc);
      return -1;
    }
  }

  if (data != nullptr && conv == "XML2FML32") {
    // *SERVICES BUFTYPECONV=XML2FML32: the service sees FML32; the
    // reply is converted back to XML before the caller sees it
    char type[8];
    if (tptypes(data, type, nullptr) != -1 && strcmp(type, "XML") == 0) {
      char *fml = tpalloc(const_cast<char *>("FML32"), nullptr,
                          std::max(len, 4096L));
      if (fml == nullptr) {
        return -1;
      }
      if (fux::xml::xml_to_fml32(data, reinterpret_cast<FBFR32 *>(fml),
                                 nullptr) == -1) {
        tpfree(fml);
        return -1;
      }
      int cd = tpacall_queue(target, svc, fml, 0, flags);
      tpfree(fml);
      if (cd != -1 && !(flags & TPNOREPLY)) {
        xml_converted.insert(cd);
      }
      return cd;
    }
  }
  return tpacall_queue(target, svc, data, len, flags);
} catch (const std::out_of_range &e) {
  TPERROR(TPENOENT, "Service %s does not exist", svc);
  return -1;
}

int client::tpacall_queue(queue_entry target, const char *svc, char *data,
                          long len, long flags) {
  // the platform sweeps the reply queue for late replies and timeout
  // markers every few calls; do the same so stragglers do not pile up
  if (--sweep_ <= 0) {
    sweep_ = 5;
    drain_late_replies();
  }

  if (data != nullptr) {
    if (tptypes(data, nullptr, nullptr) == -1) {
      return -1;
    }
  }

  rq.set_data(data, len);
  rq->mtype = target.mtype;
  rq->caller = getpid();
  checked_copy(svc, rq->servicename);
  if (flags & TPNOREPLY) {
    rq->replyq = -1;
    rq->cd = 0;
  } else {
    rq->replyq = rpid;
    rq->cd = cds.allocate();
    if (rq->cd == -1) {
      return -1;
    }
  }
  if (!(flags & TPNOTRAN) && fux::tx::transactional()) {
    rq->flags |= TPTRAN;
    rq->gttid = fux::tx::gttid();
  } else {
    rq->flags = 0;
    rq->gttid = fux::bad_gttid;
  }

  if (fux::ipc::qsend(target.msqid, rq, next_blocktime(), to_flags(flags))) {
    fux::atmi::reset_tperrno();
    return rq->cd;
  }

  if (flags & TPNOBLOCK) {
    TPERROR(TPEBLOCK, "Request queue is full");
  } else {
    TPERROR(TPETIME, "Failed to send within timeout");
  }

  if (rq->cd != 0) {
    cds.release(rq->cd);
  }
  return -1;
}

int client::tpgetrply(int *cd, char **data, long *len, long flags) {
  constexpr long valid =
      TPNOBLOCK | TPGETANY | TPNOTIME | TPSIGRSTRT | TPNOCHANGE;
  if (flags & ~valid) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpgetrply");
    return -1;
  }
  if (data == nullptr) {
    TPERROR(TPEINVAL, "data is null");
  }
  if (tptypes(*data, nullptr, nullptr) == -1) {
    return -1;
  }

  while (true) {
    fux::ipc::msg res;

    // Lookup buffered responses first
    bool has_res = false;
    if (flags & TPGETANY) {
      int c = cds.any_buffered();
      if (c != -1) {
        res = std::move(cds.buffered(c));
        has_res = true;
      }
    } else {
      if (cds.check(*cd) == -1) {
        return -1;
      }
      if (cds.is_buffered(*cd)) {
        res = std::move(cds.buffered(*cd));
        has_res = true;
      }
    }

    if (!has_res) {
      // TPGETANY with nothing outstanding is a protocol error on the
      // platform (pinned against Oracle 12.2.2), never a wait
      if ((flags & TPGETANY) && cds.empty()) {
        TPERROR(TPEPROTO, "No outstanding calls for TPGETANY");
        return -1;
      }
      // TPNOTIME waits without a deadline: leave the accesser without
      // a reply-wait timeout so the reaper never fires
      if (flags & TPNOTIME) {
        mibcon_.accessers().at(client_).rpid_timeout = INVALID_TIME;
      } else {
        mibcon_.accessers().at(client_).rpid_timeout =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(next_blocktime());
      }
      try {
        if (flags & TPNOBLOCK) {
          fux::ipc::qrecv(rpid, res, -fux::ipc::reply_limit, IPC_NOWAIT);
        } else {
          fux::ipc::qrecv(rpid, res, -fux::ipc::reply_limit, 0);
        }
      } catch (const std::system_error &) {
        mibcon_.accessers().at(client_).rpid_timeout = INVALID_TIME;
        TPERROR(TPEBLOCK, "No reply available");
        return -1;
      }
      mibcon_.accessers().at(client_).rpid_timeout = INVALID_TIME;
    }

    if (res->cat == fux::ipc::unsol) {
      // an event or notification arrived while waiting for a reply;
      // deliver it and keep waiting
      dispatch_unsol(res);
      continue;
    }

    if (res->cat == fux::ipc::unblock) {
      // the marker is client-scoped (the BBL does not know the call).
      // A timed-out call stays outstanding: it can be waited again and
      // its late reply is still deliverable to that wait (pinned by
      // install-tests/blocktime). Abandonment is tpcancel's job - that
      // is what protects the descriptor from reuse and makes the late
      // reply discardable.
      if (flags & TPGETANY) {
        *cd = 0;
      } else {
        userlog("Received time-out for cd=%d", *cd);
      }
      TPERROR(TPETIME, "Timeout message received");
      return -1;
    }

    // a late reply for a call the caller already abandoned: discard it,
    // free the descriptor, and keep waiting
    if (cds.is_stale(res->cd)) {
      userlog("discarding late reply for call descriptor %d", res->cd);
      cds.discard_late(res->cd);
      continue;
    }

    // Did not receive the correct response, try again
    if (!(flags & TPGETANY)) {
      if (*cd != res->cd) {
        cds.buffer(std::move(res));
        continue;
      }
    }

    bool backconvert = xml_converted.erase(res->cd) > 0;
    if (backconvert) {
      char *tmp = tpalloc(const_cast<char *>("FML32"), nullptr,
                          static_cast<long>(res.size_data()) + 1024);
      if (tmp == nullptr) {
        return -1;
      }
      res.get_data(&tmp);
      if (fux::xml::fml32_to_xml(reinterpret_cast<FBFR32 *>(tmp), *data,
                                 fux::mem::allocsize(*data)) == -1) {
        tpfree(tmp);
        return -1;
      }
      tpfree(tmp);
    } else {
      res.get_data(data);
    }
    tpurcode = res->rcode;
    *cd = res->cd;
    cds.release(*cd);
    if (len != nullptr) {
      // the wire carries the typed-buffer export (header + data); the
      // caller gets the logical data length, like the platform
      *len = std::max(0L, static_cast<long>(res.size_data()) -
                               fux::mem::headersize());
      if (backconvert) {
        *len = static_cast<long>(strlen(*data)) + 1;
      }
    }
    if (res->rval == TPMINVAL) {
      fux::atmi::reset_tperrno();
      return 0;
    } else {
      TPERROR(res->rval, "Service failed with %d", res->rval);
    }
    return -1;
  }
}

int client::tpcancel(int cd) {
  xml_converted.erase(cd);
  if (cds.is_stale(cd)) {
    // already abandoned; cancelling again is a harmless no-op
    fux::atmi::reset_tperrno();
    return 0;
  }
  if (cds.check(cd) == -1) {
    return -1;
  }
  // the service may still reply: protect the descriptor until the
  // late reply is discarded
  cds.mark_stale(cd);
  fux::atmi::reset_tperrno();
  return 0;
}

size_t client::dispatch_unsol(fux::ipc::msg &res) {
  if (unsol_handler_ == nullptr) {
    return 0;
  }
  auto *buf = tpalloc(const_cast<char *>("CARRAY"), nullptr,
                      static_cast<long>(res.size_data()) + 64);
  if (buf == nullptr) {
    return 0;
  }
  long olen = 0;
  if (tpimport(res->data, res.size_data(), &buf, &olen, 0) == -1) {
    userlog("tpchkunsol: cannot import unsolicited message: %s",
            tpstrerror(tperrno));
    tpfree(buf);
    return 0;
  }
  unsol_handler_(buf, olen, 0);
  tpfree(buf);
  return 1;
}

int client::chkunsol() {
  int delivered = 0;
  while (true) {
    fux::ipc::msg res;
    try {
      fux::ipc::qrecv(rpid, res, -fux::ipc::reply_limit, IPC_NOWAIT);
    } catch (const std::system_error &) {
      break;  // nothing queued right now
    }
    if (res->cat == fux::ipc::unsol) {
      delivered += static_cast<int>(dispatch_unsol(res));
    } else if (res->cat == fux::ipc::unblock) {
      // a raced time-out marker for a wait that already abandoned its
      // descriptor; drop it like drain_late_replies does
    } else if (cds.is_stale(res->cd)) {
      userlog("discarding late reply for call descriptor %d", res->cd);
      cds.discard_late(res->cd);
    } else {
      cds.buffer(std::move(res));
    }
  }
  fux::atmi::reset_tperrno();
  return delivered;
}

int client::tpconnect(const char *svc, char *data, long len, long flags) {
  if (svc == nullptr || svc[0] == '\0') {
    TPERROR(TPEINVAL, "svc is null or empty");
    return -1;
  }
  // pinned against Oracle 12.2.2 (install-tests/flags + conv): ONLY an
  // explicit single direction is accepted - one of the mode bits;
  // flags=0 is rejected outright, both bits together are rejected, as
  // are the timing/call flags
  if (flags != TPSENDONLY && flags != TPRECVONLY) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpconnect");
    return -1;
  }
  try {
    auto target = repo_.get_queue(-1, svc);

    // descriptor ids come from the bulletin board counter so a stale
    // message from a dead conversation can never alias a new one
    int cd = fux::ipc::conv_base +
             static_cast<int>(mibcon_.genuid() % (1 << 20));
    conv_state st{target.msqid, true, !(flags & TPRECVONLY)};
    convs_.emplace(cd, st);

    fux::ipc::msg m;
    m->mtype = target.mtype;
    m->cat = fux::ipc::application;
    m->caller = getpid();
    m->flags = TPCONV;
    if (flags & TPRECVONLY) {
      m->flags |= TPRECVONLY;
    }
    m->cd = cd;
    m->replyq = rpid;
    checked_copy(svc, m->servicename);
    m.set_data(data, len);
    if (!fux::ipc::qsend(target.msqid, m, next_blocktime(), to_flags(flags))) {
      convs_.erase(cd);
      if (flags & TPNOBLOCK) {
        TPERROR(TPEBLOCK, "Request queue is full");
      } else {
        TPERROR(TPETIME, "Failed to send within timeout");
      }
      return -1;
    }
    fux::atmi::reset_tperrno();
    return cd;
  } catch (const std::out_of_range &) {
    TPERROR(TPENOENT, "Service %s does not exist", svc);
    return -1;
  }
}

int client::tpsend(int cd, char *data, long len, long flags, long *revent) {
  // pinned against Oracle 12.2.2: send flags plus the mode flip to
  // receive-only; TPSENDONLY is not honored here (it describes the
  // state the peer put the conversation into)
  constexpr long valid = TPSIGRSTRT | TPNOBLOCK | TPNOTIME | TPRECVONLY;
  if (flags & ~valid) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpsend");
    return -1;
  }
  if (revent != nullptr) {
    *revent = 0;
  }
  auto it = convs_.find(cd);
  if (it == convs_.end() || !it->second.open) {
    TPERROR(TPEBADDESC, "Unknown or closed conversation %d", cd);
    return -1;
  }
  if (!it->second.send_mine) {
    TPERROR(TPEPROTO, "No send permission on conversation %d", cd);
    return -1;
  }

  fux::ipc::msg m;
  m->mtype = fux::ipc::conv_mtype(cd);
  m->cat = fux::ipc::application;
  m->flags = 0;
  m->cd = cd;
  m->revent = (flags & TPRECVONLY) ? TPEV_SENDONLY : 0;
  m.set_data(data, len);
  if (!fux::ipc::qsend(it->second.msqid, m, next_blocktime(), to_flags(flags))) {
    if (flags & TPNOBLOCK) {
      TPERROR(TPEBLOCK, "Request queue is full");
    } else {
      TPERROR(TPETIME, "Failed to send within timeout");
    }
    return -1;
  }
  if (flags & TPRECVONLY) {
    it->second.send_mine = false;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

int client::tprecv(int cd, char **data, long *len, long flags, long *revent) {
  if (revent == nullptr) {
    TPERROR(TPEINVAL, "revent is null");
    return -1;
  }
  *revent = 0;
  // pinned against Oracle 12.2.2: TPNOCHANGE is honored on receive in
  // addition to the timing flags; TPACK is rejected (isolated probe -
  // an earlier sequence-dependent run showed it accepted, see the
  // flags investigation note)
  constexpr long valid = TPSIGRSTRT | TPNOBLOCK | TPNOTIME | TPNOCHANGE;
  if (flags & ~valid) {
    TPERROR(TPEINVAL, "Invalid flags passed to tprecv");
    return -1;
  }
  auto it = convs_.find(cd);
  if (it == convs_.end() || !it->second.open) {
    TPERROR(TPEBADDESC, "Unknown or closed conversation %d", cd);
    return -1;
  }

  fux::ipc::msg res;
  try {
    fux::ipc::qrecv(rpid, res, cd, (flags & TPNOBLOCK) ? IPC_NOWAIT : 0);
  } catch (const std::system_error &) {
    TPERROR(TPEBLOCK, "No message available");
    return -1;
  }

  tpurcode = res->rcode;
  if (res->revent != 0) {
    *revent = res->revent;
    if (res->revent & TPEV_SENDONLY) {
      it->second.send_mine = true;
    }
    if (res->revent &
        (TPEV_SVCSUCC | TPEV_SVCFAIL | TPEV_SVCERR | TPEV_DISCONIMM)) {
      it->second.open = false;
    }
  }
  if (static_cast<long>(res.size_data()) > fux::mem::headersize()) {
    if (data == nullptr || *data == nullptr) {
      TPERROR(TPEINVAL, "data must point to a buffer allocated with tpalloc");
      return -1;
    }
    long olen = 0;
    if (tpimport(res->data, res.size_data(), data, &olen, 0) == -1) {
      return -1;
    }
    if (len != nullptr) {
      *len = std::max(0L, static_cast<long>(res.size_data()) -
                              fux::mem::headersize());
    }
  } else if (len != nullptr) {
    *len = 0;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

int client::tpdiscon(int cd) {
  auto it = convs_.find(cd);
  if (it == convs_.end() || !it->second.open) {
    TPERROR(TPEBADDESC, "Unknown or closed conversation %d", cd);
    return -1;
  }
  fux::ipc::msg m;
  m->mtype = fux::ipc::conv_mtype(cd);
  m->cat = fux::ipc::application;
  m->cd = cd;
  m->revent = TPEV_DISCONIMM;
  m.set_data(nullptr, 0);
  if (!fux::ipc::qsend(it->second.msqid, m, 0, fux::ipc::flags::notime)) {
    TPERROR(TPEOS, "Cannot disconnect conversation %d: %s", cd,
            strerror(errno));
    return -1;
  }
  it->second.open = false;
  fux::atmi::reset_tperrno();
  return 0;
}

void client::disconnect_convs() {
  for (auto &kv : convs_) {
    if (!kv.second.open) {
      continue;
    }
    fux::ipc::msg m;
    m->mtype = fux::ipc::conv_mtype(kv.first);
    m->cat = fux::ipc::application;
    m->cd = kv.first;
    m->revent = TPEV_DISCONIMM;
    m.set_data(nullptr, 0);
    fux::ipc::qsend(kv.second.msqid, m, 0, fux::ipc::flags::notime);
    kv.second.open = false;
  }
}

void client::drain_late_replies() {
  while (true) {
    fux::ipc::msg res;
    try {
      fux::ipc::qrecv(rpid, res, -fux::ipc::reply_limit, IPC_NOWAIT);
    } catch (const std::system_error &) {
      break;  // nothing queued right now
    }
    if (res->cat == fux::ipc::unsol) {
      // event delivery piggybacks on the periodic sweep so a busy caller
      // never has to wait for a tpchkunsol to see notifications
      dispatch_unsol(res);
    } else if (res->cat == fux::ipc::unblock) {
      // client-scoped marker without a call descriptor; the wait that
      // timed out already abandoned its descriptor when it consumed
      // its own marker, so a straggler here only means a raced one -
      // drop it
    } else if (cds.is_stale(res->cd)) {
      userlog("discarding late reply for call descriptor %d", res->cd);
      cds.discard_late(res->cd);
    } else {
      // a reply for a live call: keep it for its waiter
      cds.buffer(std::move(res));
    }
  }
}

int client::tpsblktime(int blktime, long flags) {
  long units = flags & TPBLK__MASK;
  long what = flags & ~TPBLK__MASK;
  long timeout;

  if (blktime < 0) {
    TPERROR(TPEINVAL, "Invalid blktime passed to tpsblktime(%d, ...)",
            blktime);
    return -1;
  }
  if (units == TPBLK_SECOND) {
    timeout = blktime * 1000;
  } else if (units == TPBLK_MILLISECOND) {
    if (fux::compat::is221) {
      // sub-second blocking units are rejected on 22.x
      TPERROR(TPEINVAL, "Invalid flags passed to tpsblktime(..., %ld)", flags);
      return -1;
    }
    timeout = blktime;
  } else {
    TPERROR(TPEINVAL, "Invalid flags passed to tpsblktime(..., %ld)", flags);
    return -1;
  }

  if (what == TPBLK_NEXT) {
    blocktime_next = timeout;
  } else if (what == TPBLK_ALL) {
    blocktime_all = timeout;
  } else {
    TPERROR(TPEINVAL, "Invalid flags passed to tpsblktime(..., %ld)", flags);
    return -1;
  }

  fux::atmi::reset_tperrno();
  return 0;
}

int client::tpgblktime(long flags) {
  long units = flags & TPBLK__MASK;
  long what = flags & ~TPBLK__MASK;
  long timeout;

  if (what == 0) {
    timeout = 0;
  } else if (what == TPBLK_NEXT) {
    timeout = blocktime_next;
  } else if (what == TPBLK_ALL) {
    timeout = blocktime_all;
  } else {
    TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
    return -1;
  }

  if (units == TPBLK_SECOND) {
    timeout /= 1000;
  } else if (units == TPBLK_MILLISECOND) {
    if (fux::compat::is221) {
      TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
      return -1;
    }
    timeout /= 1;
  } else {
    TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
    return -1;
  }

  fux::atmi::reset_tperrno();
  return timeout;
}

long client::next_blocktime() {
  long blocktime = 0;
  if (blocktime_next != 0) {
    blocktime = blocktime_next;
    blocktime_next = 0;
  } else if (blocktime_all != 0) {
    blocktime = blocktime_all;
  } else {
    blocktime = mibcon_.mach().blocktime;
  }
  return blocktime;
}

fux::ipc::flags client::to_flags(long flags) {
  // send disposition is orthogonal to TPNOTIME (which governs the
  // reply wait, honored in tpgetrply): TPNOBLOCK always wins here
  if (flags & TPNOBLOCK) {
    return fux::ipc::flags::noblock;
  }
  if (flags & TPNOTIME) {
    return fux::ipc::flags::notime;
  }
  return fux::ipc::flags::noflags;
}

static thread_local std::shared_ptr<client> current_client;
static client &getclient(const char *cltname = nullptr,
                         const char *usrname = nullptr) {
  if (!current_client.get()) {
    current_client = std::make_shared<client>(getmib(), cltname, usrname);
  }
  return *current_client;
}

int tpinit(TPINIT *tpinfo) {
  if (fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from server", __func__);
    return -1;
  }
  if (fux::ws::ws_naddr() != nullptr) {
    // workstation mode: connect to the WSL listener instead of the
    // local bulletin board
    return fux::atmi::exception_boundary(
        [&] {
          return fux::ws::get_wsconn().login(
              tpinfo != nullptr ? tpinfo->cltname : nullptr,
              tpinfo != nullptr ? tpinfo->usrname : nullptr,
              tpinfo != nullptr ? tpinfo->grpname : nullptr,
              tpinfo != nullptr ? tpinfo->passwd : nullptr);
        },
        -1);
  }
  fux::trace::event("{ tpinit(0)");
  int rc = fux::atmi::exception_boundary(
      [&] {
        getclient(tpinfo != nullptr ? tpinfo->cltname : nullptr,
                  tpinfo != nullptr ? tpinfo->usrname : nullptr);
        return 0;
      },
      0);
  fux::trace::event("} tpinit = %d", rc);
  return rc;
}

int tpterm() {
  if (fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from server", __func__);
    return -1;
  }
  if (fux::ws::ws_naddr() != nullptr) {
    fux::ws::get_wsconn().logout();
    fux::atmi::reset_tperrno();
    return 0;
  }
  fux::trace::event("{ tpterm()");
  int rc = fux::atmi::exception_boundary(
      [&] {
        current_client.reset();
        return 0;
      },
      0);
  fux::trace::event("} tpterm = %d", rc);
  return rc;
}

int tpappthrinit(TPINIT * /*tpinfo*/) {
  if (!fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from client", __func__);
    return -1;
  }
  fux::atmi::exception_boundary([&] { getclient(); });
  return 0;
}

int tpappthrterm() {
  if (!fux::is_server()) {
    TPERROR(TPEPROTO, "%s called from client", __func__);
    return -1;
  }
  fux::atmi::exception_boundary([&] { current_client.reset(); });
  return 0;
}

int tpcancel(int cd) {
  int rc;
  if (fux::ws::ws_naddr() != nullptr) {
    rc = fux::atmi::exception_boundary(
        [&] { return fux::ws::get_wsconn().tpcancel(cd); }, -1);
  } else {
    rc = fux::atmi::exception_boundary(
        [&] { return getclient().tpcancel(cd); }, -1);
  }
  FUX_PROBE2(tpcancel, cd, rc);
  return rc;
}

int tpacall_group(int grpno, char *svc, char *data, long len, long flags) {
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpacall_group(grpno, svc, data, len, flags); },
      -1);
}

int tpacall_queue(int msqid, const char *svc, char *data, long len,
                  long flags) {
  queue_entry target{0, msqid, 1};
  return fux::atmi::exception_boundary(
      [&] {
        return getclient().tpacall_queue(target, svc, data, len, flags);
      },
      -1);
}

int tpacall(char *svc, char *data, long len, long flags) {
  fux::trace::event("{ tpacall(\"%s\", %ld)", svc, len);
  FUX_PROBE3(tpacall__entry, svc, len, flags);
  // pinned against Oracle 12.2.2 on a booted domain (probe
  // tmp/flagprobe/mask3): the call flags plus TPNOREPLY only - notably
  // NOT TPNOCHANGE/TPNOCOPY, which tpcall alone accepts
  constexpr long valid =
      TPNOBLOCK | TPSIGRSTRT | TPNOREPLY | TPNOTRAN | TPNOTIME;
  if (flags & ~valid) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpacall");
    return -1;
  }
  int rc = 0;
  if (fux::ws::ws_naddr() != nullptr) {
    rc = fux::atmi::exception_boundary(
        [&] { return fux::ws::get_wsconn().tpacall(svc, data, len, flags); },
        -1);
  } else {
    rc = fux::atmi::exception_boundary(
        [&] { return getclient().tpacall_group(-1, svc, data, len, flags); },
        -1);
  }
  fux::trace::event("} tpacall = %d", rc);
  return rc;
}

int tpgetrply(int *cd, char **data, long *len, long flags) {
  fux::trace::event("{ tpgetrply(%d)", cd != nullptr ? *cd : -1);
  constexpr long valid =
      TPNOBLOCK | TPGETANY | TPNOTIME | TPSIGRSTRT | TPNOCHANGE;
  if (flags & ~valid) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpgetrply");
    return -1;
  }
  int rc = 0;
  if (fux::ws::ws_naddr() != nullptr) {
    rc = fux::atmi::exception_boundary(
        [&] { return fux::ws::get_wsconn().tpgetrply(cd, data, len, flags); },
        -1);
  } else {
    rc = fux::atmi::exception_boundary(
        [&] {
          // TODO: if no transaction calls, use false
          fux::suspend_guard g(true);
          return getclient().tpgetrply(cd, data, len, flags);
        },
        -1);
  }
  fux::trace::event("} tpgetrply = %d", rc);
  FUX_PROBE2(tpgetrply__return, cd != nullptr ? *cd : -1, rc);
  return rc;
}
int tpcall(char *svc, char *idata, long ilen, char **odata, long *olen,
           long flags) {
  fux::trace::event("{ tpcall(\"%s\", %ld)", svc, ilen);
  FUX_PROBE2(tpcall__entry, svc, ilen);
  // pinned against Oracle 12.2.2 (probe tmp/flagprobe/mask): the
  // documented tpcall flags plus TPNOCOPY; TPNOREPLY is tpacall-only
  constexpr long valid = TPNOBLOCK | TPSIGRSTRT | TPNOTRAN | TPNOTIME |
                         TPNOCHANGE | TPNOCOPY;
  if (flags & ~valid) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpcall");
    return -1;
  }
  int rc = 0;
  if (fux::ws::ws_naddr() != nullptr) {
    rc = fux::atmi::exception_boundary(
        [&] {
          int cd = fux::ws::get_wsconn().tpacall(svc, idata, ilen, flags);
          if (cd == -1 || (flags & TPNOREPLY)) {
            return cd == -1 ? -1 : 0;
          }
          return fux::ws::get_wsconn().tpgetrply(&cd, odata, olen, flags);
        },
        -1);
  } else {
    rc = fux::atmi::exception_boundary(
        [&] {
          fux::suspend_guard g(!(flags & TPNOTRAN));
          int cd = getclient().tpacall_group(-1, svc, idata, ilen, flags);
          if (cd == -1) {
            return -1;
          }
          if (flags & TPNOREPLY) {
            return 0;
          }
          // only the reply-side bits are meaningful to the internal
          // wait - the platform's tpcall validates one wide flag word
          // then waits with the getrply subset (TPNOTRAN et al. would
          // be rejected by a plain tpgetrply, pinned 12.2.2)
          return getclient().tpgetrply(
              &cd, odata, olen,
              flags & (TPNOBLOCK | TPNOTIME | TPSIGRSTRT | TPNOCHANGE));
        },
        -1);
  }
  fux::trace::event("} tpcall = %d", rc);
  return rc;
}

int tpsblktime(int blktime, long flags) {
  if (fux::ws::ws_naddr() != nullptr) {
    return fux::atmi::exception_boundary(
        [&] { return fux::ws::get_wsconn().tpsblktime(blktime, flags); }, -1);
  }
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpsblktime(blktime, flags); }, -1);
}

int tpgblktime(long flags) {
  if (fux::ws::ws_naddr() != nullptr) {
    return fux::atmi::exception_boundary(
        [&] { return fux::ws::get_wsconn().tpgblktime(flags); }, -1);
  }
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpgblktime(flags); }, -1);
}

queue_entry get_queue(const char *svc) { return getclient().get_queue(svc); }

int tpgetctxt(TPCONTEXT_T *context, long /*flags*/) {
  // Single-context implementation: a process always has the initial
  // context, like Oracle Tuxedo without TPMULTICONTEXTS.
  if (context != nullptr) {
    *context = 0;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

int tpsetctxt(TPCONTEXT_T context, long /*flags*/) {
  if (context != 0) {
    TPERROR(TPEPROTO, "only the initial context is supported");
    return -1;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

void (*tpsetunsol(void (*func)(char *data, long len, long flags)))(char *,
                                                                    long,
                                                                    long) {
  if (fux::ws::ws_naddr() != nullptr) {
    TPERROR(TPESYSTEM, "tpsetunsol is not available in workstation mode");
    return nullptr;
  }
  // the API never throws: without a bulletin board the handler cannot
  // be registered and the platform-style answer is nullptr + TPESYSTEM
  return fux::atmi::exception_boundary(
      [&]() -> void (*)(char *, long, long) {
        auto prev = getclient().setunsol(func);
        fux::atmi::reset_tperrno();
        return prev;
      },
      static_cast<void (*)(char *, long, long)>(nullptr));
}

int tpchkunsol() {
  if (fux::ws::ws_naddr() != nullptr) {
    // no unsolicited channel over a workstation link yet
    fux::atmi::reset_tperrno();
    return 0;
  }
  return fux::atmi::exception_boundary([&] { return getclient().chkunsol(); },
                                       -1);
}

namespace fux::clnt {
int tpconnect(char *svc, char *data, long len, long flags) {
  if (fux::ws::ws_naddr() != nullptr) {
    TPERROR(TPESYSTEM, "tpconnect is not available in workstation mode");
    return -1;
  }
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpconnect(svc, data, len, flags); }, -1);
}

int tpsend(int cd, char *data, long len, long flags, long *revent) {
  if (fux::ws::ws_naddr() != nullptr) {
    TPERROR(TPESYSTEM, "tpsend is not available in workstation mode");
    return -1;
  }
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpsend(cd, data, len, flags, revent); }, -1);
}

int tprecv(int cd, char **data, long *len, long flags, long *revent) {
  if (fux::ws::ws_naddr() != nullptr) {
    TPERROR(TPESYSTEM, "tprecv is not available in workstation mode");
    return -1;
  }
  return fux::atmi::exception_boundary(
      [&] { return getclient().tprecv(cd, data, len, flags, revent); }, -1);
}

int tpdiscon(int cd) {
  if (fux::ws::ws_naddr() != nullptr) {
    TPERROR(TPESYSTEM, "tpdiscon is not available in workstation mode");
    return -1;
  }
  return fux::atmi::exception_boundary(
      [&] { return getclient().tpdiscon(cd); }, -1);
}
}  // namespace fux::clnt

int tpconnect(char *svc, char *data, long len, long flags) {
  return fux::clnt::tpconnect(svc, data, len, flags);
}
