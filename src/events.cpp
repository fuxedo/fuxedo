// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// Client-side EventBroker entry points. Subscriptions go to the event
// server (TMUSREVT/TMSYSEVT) as ordinary service calls on .TMEVT - one
// writer keeps the on-disk database append-only and concurrency-safe.
// Posts and notifications read the subscription table in the bulletin
// board and push straight into the subscriber's unsolicited channel.

#include <signal.h>

#include <cerrno>
#include <cstring>

#include <atmi.h>
#include <fml32.h>
#include <tpadm.h>
#include <userlog.h>

#include "client.h"
#include "events.h"
#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "probes.h"
#include "trace.h"
#include "ws.h"

namespace {

constexpr char EVT_SVC[] = ".TMEVT";

// CLIENTID string form recorded in the subscription database; the first
// token carries the subscriber pid
std::string clientid_str(pid_t pid) {
  char buf[64];
  snprintf(buf, sizeof(buf), "0x%lx 0x0 0x2 0x0",
           static_cast<unsigned long>(pid));
  return buf;
}

bool ws_mode() { return fux::ws::ws_naddr() != nullptr; }

int tpsubscribe_impl(char *eventexpr, char *filter, TPEVCTL *ctl, long flags) {
  if (eventexpr == nullptr || eventexpr[0] == '\0') {
    TPERROR(TPEINVAL, "eventexpr is null or empty");
    return -1;
  }
  // exactly one delivery class, optionally ORed with TPEVPERSIST
  constexpr long valid = TPEVSERVICE | TPEVQUEUE | TPEVPERSIST;
  if ((flags & ~valid) != 0 ||
      ((flags & (TPEVSERVICE | TPEVQUEUE)) == (TPEVSERVICE | TPEVQUEUE)) ||
      (flags & (TPEVSERVICE | TPEVQUEUE)) == 0) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpsubscribe");
    return -1;
  }
  if (ctl != nullptr && ctl->flags != 0) {
    TPERROR(TPEINVAL, "Unsupported flags in evctl");
    return -1;
  }

  fux::fml32buf rq;
  rq.put(TA_CLASS, 0, "T_EVENT_CLIENT");
  rq.put(TA_EVENT_EXPR, 0, eventexpr);
  if (filter != nullptr && filter[0] != '\0') {
    rq.put(TA_EVENT_FILTER, 0, filter);
  }
  rq.put(TA_CLIENTID, 0, clientid_str(getpid()));
  rq.put(TA_FLAGS, 0, 0L);
  fux::tpcall(EVT_SVC, rq, 0);
  return static_cast<int>(rq.get<long>(TA_SUBSCRIPTION_HANDLE, 0));
}

}  // namespace

namespace fux {
namespace events {

int deliver(::mib &m, const char *eventname, char *data, long len) {
  auto lock = m.data_lock();
  auto &events = m.events();

  fux::ipc::msg note;
  note->mtype = 1;
  note->cat = fux::ipc::unsol;
  checked_copy(eventname, note->servicename);
  note.set_data(data, len);

  for (size_t i = 0; i < events.len; i++) {
    auto &sub = events.at(i);
    if (!sub.used || strcmp(sub.eventname, eventname) != 0) {
      continue;
    }
    if (sub.rpid < 0) {
      // subscriber's reply queue unknown (client entry gone); drop
      events.free(i);
      continue;
    }
    if (kill(sub.pid, 0) == -1 && errno == ESRCH) {
      // subscriber vanished without unsubscribing
      events.free(i);
      continue;
    }
    if (sub.filter[0] != '\0') {
      if (data == nullptr) {
        continue;
      }
      char type[16];
      int ttrc = tptypes(data, type, nullptr);
      if (ttrc == -1) {
        return -1;
      }
      if (strcmp(type, "FML32") != 0) {
        continue;
      }
      try {
        if (sub.filter[0] != '\0' && sub.filtertree[0] == '\0') {
          // entry replayed where field tables were unavailable (or an
          // older record): compile lazily, the lock is held
          char *compiled = Fboolco32(sub.filter);
          unsigned tn = compiled == nullptr
                            ? 0
                            : static_cast<unsigned char>(compiled[0]) |
                                  (static_cast<unsigned char>(compiled[1])
                                   << 8);
          if (tn < 4 || tn + 1 > sizeof(sub.filtertree)) {
            continue;
          }
          std::copy_n(compiled, tn, sub.filtertree);
          sub.filtertree[tn] = '\0';
        }
        if (sub.filtertree[0] != '\0') {
          int ev = Fboolev32(reinterpret_cast<FBFR32 *>(data), sub.filtertree);
          if (ev <= 0) {
            continue;
          }
        }
      } catch (const std::exception &e) {
        continue;
      }
    }
    if (!fux::ipc::qsend(sub.rpid, note, 0, fux::ipc::flags::noblock)) {
      userlog("tppost: dropping subscriber pid %d: %s", sub.pid,
              strerror(errno));
      if (errno == EIDRM || errno == ESRCH || errno == EINVAL) {
        events.free(i);
      }
    }
  }
  fux::atmi::reset_tperrno();
  return 0;
}

}  // namespace events
}  // namespace fux

long tpsubscribe(char *eventexpr, char *filter, TPEVCTL *ctl, long flags) {
  fux::trace::event("{ tpsubscribe(\"%s\")", eventexpr);
  FUX_PROBE1(tpsubscribe__entry, eventexpr);
  // no ws_mode gate: tpcall routes the .TMEVT request through WSL when
  // WSNADDR is set, so workstation clients can subscribe too
  long rc = fux::atmi::exception_boundary(
      [&] { return static_cast<long>(tpsubscribe_impl(eventexpr, filter, ctl, flags)); },
      -1L);
  fux::trace::event("} tpsubscribe = %ld", rc);
  return rc;
}

int tpunsubscribe(long subscription, long flags) {
  fux::trace::event("{ tpunsubscribe(%ld)", subscription);
  int rc = fux::atmi::exception_boundary(
      [&] {
        if (flags != 0) {
          TPERROR(TPEINVAL, "Invalid flags passed to tpunsubscribe");
          return -1;
        }

        fux::fml32buf rq;
        rq.put(TA_CLASS, 0, "T_EVENT_CLIENT");
        rq.put(TA_SUBSCRIPTION_HANDLE, 0, subscription);
        rq.put(TA_CLIENTID, 0, clientid_str(getpid()));
        fux::tpcall(EVT_SVC, rq, 0);
        fux::atmi::reset_tperrno();
        return 0;
      },
      -1);
  fux::trace::event("} tpunsubscribe = %d", rc);
  return rc;
}

int tppost(char *eventname, char *data, long len, long flags) {
  (void)flags;  // no flag values are defined; the platform ignores them
  fux::trace::event("{ tppost(\"%s\", %ld)", eventname, len);
  FUX_PROBE2(tppost__entry, eventname, len);
  int rc = -1;
  if (ws_mode()) {
    // workstation clients have no bulletin board: the event server
    // matches subscribers and delivers on our behalf
    rc = fux::atmi::exception_boundary(
        [&] {
          if (eventname == nullptr || eventname[0] == '\0') {
            TPERROR(TPEINVAL, "eventname is null or empty");
            return -1;
          }
          fux::fml32buf rq;
          rq.put(TA_CLASS, 0, "T_EVENT_CLIENT");
          rq.put(TA_EVENT_EXPR, 0, eventname);
          fux::ipc::msg note;
          note.set_data(data, len);
          rq.put(fux::evw::f_data(), 0,
                 std::string(note->data, note.size_data()));
          fux::tpcall(EVT_SVC, rq, 0);
          fux::atmi::reset_tperrno();
          return 0;
        },
        -1);
  } else {
    rc = fux::atmi::exception_boundary(
        [&] {
          if (eventname == nullptr || eventname[0] == '\0') {
            TPERROR(TPEINVAL, "eventname is null or empty");
            return -1;
          }
          auto &m = getmib();
          return fux::events::deliver(m, eventname, data, len);
        },
        -1);
  }
  fux::trace::event("} tppost = %d", rc);
  return rc;
}

int tpnotify(CLIENTID *cltid, char *data, long len, long flags) {
  fux::trace::event("{ tpnotify(%ld)", cltid ? cltid->clientdata[0] : -1L);
  int rc = -1;
  if (ws_mode()) {
    TPERROR(TPESYSTEM, "tpnotify is not available in workstation mode");
  } else {
    rc = fux::atmi::exception_boundary(
        [&] {
          if (cltid == nullptr) {
            TPERROR(TPEINVAL, "cltid is null");
            return -1;
          }
          if (flags != 0) {
            TPERROR(TPEINVAL, "Invalid flags passed to tpnotify");
            return -1;
          }
          pid_t pid = static_cast<pid_t>(cltid->clientdata[0]);

          auto &m = getmib();
          auto lock = m.data_lock();
          int rpid = -1;
          for (size_t i = 0; i < m.accessers().length(); i++) {
            auto &a = m.accessers().at(i);
            if (a.valid() && a.pid == pid) {
              rpid = a.rpid;
              break;
            }
          }
          if (rpid == -1) {
            TPERROR(TPENOENT, "No such client %d", pid);
            return -1;
          }

          fux::ipc::msg note;
          note->mtype = 1;
          note->cat = fux::ipc::unsol;
          note->servicename[0] = '\0';
          note.set_data(data, len);
          if (!fux::ipc::qsend(rpid, note, 0, fux::ipc::flags::noblock)) {
            TPERROR(TPEOS, "Cannot notify client %d: %s", pid,
                    strerror(errno));
            return -1;
          }
          fux::atmi::reset_tperrno();
          return 0;
        },
        -1);
  }
  fux::trace::event("} tpnotify = %d", rc);
  return rc;
}

int tpbroadcast(char * /*lmid*/, char *usrname, char *cltname, char *data,
                long len, long flags) {
  (void)flags;  // no flag values are defined; the platform ignores them
  fux::trace::event("{ tpbroadcast(\"%s\")",
                    cltname != nullptr ? cltname : "");
  int rc = -1;
  if (ws_mode()) {
    TPERROR(TPESYSTEM, "tpbroadcast is not available in workstation mode");
  } else {
    rc = fux::atmi::exception_boundary(
        [&] {
          auto &m = getmib();
          auto lock = m.data_lock();

          fux::ipc::msg note;
          note->mtype = 1;
          note->cat = fux::ipc::unsol;
          note->servicename[0] = '\0';
          note.set_data(data, len);

          for (size_t i = 0; i < m.accessers().length(); i++) {
            auto &a = m.accessers().at(i);
            if (!a.valid() || a.rpid < 0) {
              continue;
            }
            // empty or NULL filter matches every client
            if (cltname != nullptr && cltname[0] != '\0' &&
                strcmp(a.cltname, cltname) != 0) {
              continue;
            }
            if (usrname != nullptr && usrname[0] != '\0' &&
                strcmp(a.usrname, usrname) != 0) {
              continue;
            }
            // lmid is accepted for signature compatibility; a single
            // machine matches all values
            if (!fux::ipc::qsend(a.rpid, note, 0, fux::ipc::flags::noblock)) {
              userlog("tpbroadcast: dropping client pid %d: %s", a.pid,
                      strerror(errno));
            }
          }
          fux::atmi::reset_tperrno();
          return 0;
        },
        -1);
  }
  fux::trace::event("} tpbroadcast = %d", rc);
  return rc;
}

// tpsetunsol/tpchkunsol live in client.cpp: they need the thread-local
// client context, which is file-local there.
