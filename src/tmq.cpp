// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xatmi.h>

#include <chrono>
#include <limits>
#include <thread>

#include <atmi.h>
#include <userlog.h>

#include "cfg.h"
#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "qm.h"
#include "tmqwire.h"
#include "trace.h"
#include "probes.h"
#include "trx.h"
#include "ws.h"

namespace fux::glob {
extern xa_switch_t *xasw;
}

namespace {

int qm_fail(TPQCTL *ctl, const fux::qm::qm_error &e) {
  if (ctl != nullptr) {
    ctl->diagnostic = e.diagnostic;
  }
  if (e.diagnostic == QMEINVAL) {
    TPERROR(TPEINVAL, "%s", e.what());
  } else if (e.diagnostic == QMENOMSG) {
    // pinned on Oracle 22.1: an empty queue is TPEDIAGNOSTIC with
    // QMENOMSG, same as the domain path below
    TPERROR(TPEDIAGNOSTIC, "%s", e.what());
  } else if (e.diagnostic == QMEBADQUEUE || e.diagnostic == QMEBADMSGID) {
    TPERROR(TPENOENT, "%s", e.what());
  } else {
    TPERROR(TPEOS, "%s", e.what());
  }
  return -1;
}

// The diagnostic travels as tpurcode when the request went through the
// TMQUEUE server; map it back the same way qm_fail does. An empty queue
// is TPETIME on both paths (matches the direct file path and the
// platform).
int remote_fail(TPQCTL *ctl, const char *what, long diag) {
  if (ctl != nullptr) {
    ctl->diagnostic = diag;
  }
  if (diag == QMEINVAL) {
    TPERROR(TPEINVAL, "%s", what);
  } else if (diag == QMENOMSG) {
    TPERROR(TPEDIAGNOSTIC, "%s", what);
  } else if (diag == QMEBADQUEUE || diag == QMEBADMSGID) {
    TPERROR(TPENOENT, "%s", what);
  } else if (diag == QMETRAN) {
    TPERROR(TPETRAN, "%s", what);
  } else {
    TPERROR(TPEOS, "%s", what);
  }
  return -1;
}

// Transactional queue operations are supported when the process runs in
// a TUXEDO/QM server group: the effects are staged and completed with the
// global transaction.
bool transactional_q() {
  return fux::tx::tx_active() && fux::glob::xasw == &tuxq_switch;
}

std::string gtrid() {
  XID xid = fux::tx::current_xid();
  xid.bqual_length = 0;
  return fux::to_string(&xid);
}

// Domain mode: when the application is running and a TMQUEUE server
// advertises "<qspace>:TMQUEUE", requests are routed through it (the
// Oracle model). Otherwise the caller talks to the queue files directly
// (standalone mode, no domain booted).
bool routed(const char *qspace, std::string *svc) {
  if (fux::ws::ws_naddr() != nullptr) {
    // workstation clients have no local bulletin board: queue requests
    // always travel to the domain's TMQUEUE through the WSL
    *svc = std::string(qspace) + ":TMQUEUE";
    return true;
  }
  static const uint32_t ipckey = [] {
    try {
      fux::cfg::tables t;
      getconfig(&t);
      return t.dom.ipckey;
    } catch (const std::exception &) {
      return 0u;
    }
  }();
  if (ipckey == 0 ||
      mib::check_node(ipckey) != node_state::running) {
    return false;
  }
  try {
    auto &m = getmib();
    std::string name = std::string(qspace) + ":TMQUEUE";
    if (m.find_service(name.c_str()) == mib::badoff) {
      return false;
    }
    *svc = name;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

}  // namespace

namespace fux::tmq {

int exec_enqueue(const char *qspace, const char *qname, TPQCTL *ctl,
                 const std::string &payload) {
  try {
    const bool txq = transactional_q();
    auto id = txq ? fux::qm::stage_enqueue(qspace, qname, ctl, payload,
                                           gtrid())
                  : fux::qm::enqueue(qspace, qname, ctl, payload);
    if ((ctl->flags & TPQMSGID) != 0) {
      memset(ctl->msgid, 0, TMMSGIDLEN);
      std::copy_n(id.c_str(), std::min(id.size(), (size_t)TMMSGIDLEN - 1),
                  ctl->msgid);
    }
    fux::atmi::reset_tperrno();
    return 0;
  } catch (const fux::qm::qm_error &e) {
    return qm_fail(ctl, e);
  } catch (const std::exception &e) {
    TPERROR(TPESYSTEM, "%s", e.what());
    return -1;
  }
}

int exec_dequeue(const char *qspace, const char *qname, TPQCTL *ctl,
                 char **data, long *len, long flags) {
  try {
    const bool txq = transactional_q();
    long wait_ms = (flags & TPQWAIT) ? 30000 : 0;
    bool stage = txq && (flags & TPQPEEK) == 0;
    auto result = fux::qm::dequeue(
        qspace, qname, ctl, ((flags & TPQPEEK) != 0) || stage, wait_ms);
    if (!result.found) {
      // the platform reports an empty queue as TPEDIAGNOSTIC with
      // QMENOMSG; TPETIME belongs to the wait deadline expiring
      ctl->diagnostic = QMENOMSG;
      TPERROR(TPEDIAGNOSTIC, "no message available on queue %s", qname);
      return -1;
    }
    if (stage) {
      fux::qm::stage_dequeue(qspace, qname, result.m.id, gtrid());
    }

    if (!result.m.payload.empty()) {
      if (tpimport(&result.m.payload[0], result.m.payload.size(), data, 0,
                   0) == -1) {
        return -1;
      }
    }
    if (len != nullptr) {
      *len = result.m.payload.size();
    }

    if ((ctl->flags & TPQMSGID) != 0) {
      memset(ctl->msgid, 0, TMMSGIDLEN);
      std::copy_n(result.m.id.c_str(),
                  std::min(result.m.id.size(), (size_t)TMMSGIDLEN - 1),
                  ctl->msgid);
    }
    if ((ctl->flags & TPQCORRID) != 0) {
      memset(ctl->corrid, 0, TMCORRIDLEN);
      std::copy_n(result.m.corrid.data(),
                  std::min(result.m.corrid.size(), (size_t)TMCORRIDLEN),
                  ctl->corrid);
    }
    if ((ctl->flags & TPQPRIORITY) != 0) {
      ctl->priority = result.m.priority;
    }
    if ((ctl->flags & TPQREPLYQ) != 0 && !result.m.replyq.empty()) {
      memset(ctl->replyqueue, 0, sizeof(ctl->replyqueue));
      std::copy_n(result.m.replyq.c_str(),
                  std::min(result.m.replyq.size(), (size_t)TMQNAMELEN),
                  ctl->replyqueue);
    }
    fux::atmi::reset_tperrno();
    return 0;
  } catch (const fux::qm::qm_error &e) {
    return qm_fail(ctl, e);
  } catch (const std::exception &e) {
    TPERROR(TPESYSTEM, "%s", e.what());
    return -1;
  }
}

}  // namespace fux::tmq

int tpenqueue(char *qspace, char *qname, TPQCTL *ctl, char *data, long len,
              long flags) {
  if (qspace == nullptr || qname == nullptr || ctl == nullptr) {
    TPERROR(TPEINVAL, "qspace, qname and ctl are required");
    return -1;
  }
  if ((flags & ~TPNOTIME) != 0) {
    TPERROR(TPEINVAL, "invalid flags %ld", flags);
    return -1;
  }
  FUX_PROBE3(tpenqueue__entry, qspace, qname, len);
  std::string svc;
  const bool via_server = routed(qspace, &svc);
  if (fux::tx::tx_active() && !transactional_q() && !via_server) {
    TPERROR(TPETRAN, "queue operations in transaction mode not supported "
                     "for this resource manager");
    ctl->diagnostic = QMETRAN;
    return -1;
  }

  fux::trace::event("{ tpenqueue(\"%s\", \"%s\")", qspace, qname);
  int trace_rc = 0;
  std::string payload;
  if (data != nullptr) {
    long used = fux::mem::bufsize(data, len);
    if (used == -1) {
      return -1;
    }
    payload.resize(used);
    if (tpexport(data, len, &payload[0], &used, 0) == -1) {
      return -1;
    }
    payload.resize(used);
  }

  if (via_server) {
    // NOTE: owning fml32buf locals must die before remote_fail runs:
    // their destructors call tpfree(), whose exception boundary resets
    // tperrno.
    long fail_diag = 0;
    {
      fux::fml32buf rq, rs;
      rq.put(fux::tmq::f_op(), 0, (long)fux::tmq::enqueue);
      rq.put(fux::tmq::f_qname(), 0, qname);
      rq.put(fux::tmq::f_ctl(), 0,
             std::string(reinterpret_cast<char *>(ctl), sizeof(*ctl)));
      rq.put(fux::tmq::f_data(), 0, payload);

      long olen = 0;
      if (::tpcall(const_cast<char *>(svc.c_str()),
                   reinterpret_cast<char *>(*rq.ptrptr()), 0,
                   reinterpret_cast<char **>(rs.ptrptr()), &olen,
                   TPNOTIME) == -1) {
        fail_diag = tpurcode;
      } else {
        std::string outctl = rs.getbytes(fux::tmq::f_ctl(), 0);
        std::copy_n(outctl.data(), std::min(outctl.size(), sizeof(*ctl)),
                    reinterpret_cast<char *>(ctl));
      }
    }
    if (fail_diag != 0) {
      return remote_fail(ctl, "tpenqueue", fail_diag);
    }
    fux::atmi::reset_tperrno();
    return 0;
  }

  trace_rc = fux::tmq::exec_enqueue(qspace, qname, ctl, payload);
  fux::trace::event("} tpenqueue = %d", trace_rc);
  return trace_rc;
}

// TPQWAIT is served by the caller, not the queue server: try a plain
// dequeue once a second until the deadline. Keeps TMQUEUE single-threaded
// and responsive, the way the platform runs its queue servers.
// TPQWAIT and TPQTIME_* live in ctl->flags (they describe deq_time); the
// flags argument carries only TPNOTIME. Returns milliseconds until the
// deadline, 0 for a single non-blocking attempt, or a negative sentinel
// when TPNOTIME lifts the limit entirely.
static long wait_deadline_ms(long flags, const TPQCTL *ctl) {
  if ((ctl->flags & TPQWAIT) == 0) {
    return 0;
  }
  if ((flags & TPNOTIME) != 0) {
    return std::numeric_limits<long>::min();
  }
  long wait_s = 30;  // TMQUEUE -t default for waits without TPQTIME_*
  if ((ctl->flags & TPQTIME_REL) != 0) {
    wait_s = ctl->deq_time;
  } else if ((ctl->flags & TPQTIME_ABS) != 0) {
    wait_s = ctl->deq_time - time(nullptr);
  }
  if (wait_s <= 0) {
    return -1;
  }
  return wait_s * 1000;
}

static int tpdequeue_once(const std::string &svc, const char *qspace,
                          const char *qname, TPQCTL *ctl, char **data,
                          long *len, long flags, bool via_server);

int tpdequeue(char *qspace, char *qname, TPQCTL *ctl, char **data, long *len,
              long flags) {
  // pinned on Oracle 22.1: the flags argument accepts only TPNOTIME;
  // the TPQ* selectors (GETBY/WAIT/TIME/PEEK) live in TPQCTL.flags and
  // are rejected here client-side with TPEINVAL
  constexpr long valid_flags = TPNOTIME;
  if (qspace == nullptr || qname == nullptr || ctl == nullptr ||
      data == nullptr) {
    TPERROR(TPEINVAL, "qspace, qname, ctl and data are required");
    return -1;
  }
  if ((flags & ~valid_flags) != 0) {
    TPERROR(TPEINVAL, "invalid flags %ld", flags);
    return -1;
  }
  if (*data == nullptr) {
    TPERROR(TPEINVAL, "*data must point to a buffer allocated with tpalloc");
    return -1;
  }

  std::string svc;
  const bool via_server = routed(qspace, &svc);
  fux::trace::event("{ tpdequeue(\"%s\", \"%s\", 0x%lx)", qspace, qname,
                    flags);
  FUX_PROBE3(tpdequeue__entry, qspace, qname, flags);
  long wait_ms = wait_deadline_ms(flags, ctl);
  long deadline = 0;  // 0 or negative: no expiry (single shot or TPNOTIME)
  if (wait_ms > 0) {
    deadline =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count() +
        wait_ms;
  }

  for (;;) {
    int rc = tpdequeue_once(svc, qspace, qname, ctl, data, len, flags,
                            via_server);
    // an empty queue reports TPEDIAGNOSTIC/QMENOMSG; that is the signal
    // to keep waiting, never TPETIME (Oracle 12.2.2 pin)
    bool empty = rc == -1 && tperrno == TPEDIAGNOSTIC &&
                 ctl->diagnostic == QMENOMSG;
    if (rc == 0 || !empty || wait_ms == 0 || wait_ms == -1) {
      fux::trace::event("} tpdequeue = %d", rc);
      return rc;
    }
    long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    if (deadline > 0 && now >= deadline) {
      // the wait deadline expired: that is what TPETIME is for
      TPERROR(TPETIME, "no message available on queue %s within the wait "
                       "deadline",
              qname);
      fux::trace::event("} tpdequeue = -1");
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

static int tpdequeue_once(const std::string &svc, const char *qspace,
                          const char *qname, TPQCTL *ctl, char **data,
                          long *len, long flags, bool via_server) {
  if (fux::tx::tx_active() && !transactional_q() && !via_server) {
    TPERROR(TPETRAN, "queue operations in transaction mode not supported "
                     "for this resource manager");
    ctl->diagnostic = QMETRAN;
    return -1;
  }

  if (via_server) {
    // See the enqueue path: locals die before the error is reported.
    long fail_diag = 0;
    std::string payload;
    {
      fux::fml32buf rq, rs;
      rq.put(fux::tmq::f_op(), 0, (long)fux::tmq::dequeue);
      rq.put(fux::tmq::f_qname(), 0, qname);
      rq.put(fux::tmq::f_ctl(), 0,
             std::string(reinterpret_cast<char *>(ctl), sizeof(*ctl)));
      rq.put(fux::tmq::f_flags(), 0, flags & ~TPQWAIT);

      long olen = 0;
      if (::tpcall(const_cast<char *>(svc.c_str()),
                   reinterpret_cast<char *>(*rq.ptrptr()), 0,
                   reinterpret_cast<char **>(rs.ptrptr()), &olen,
                   TPNOTIME) == -1) {
        fail_diag = tpurcode;
      } else {
        std::string outctl = rs.getbytes(fux::tmq::f_ctl(), 0);
        std::copy_n(outctl.data(), std::min(outctl.size(), sizeof(*ctl)),
                    reinterpret_cast<char *>(ctl));
        if (rs.count(fux::tmq::f_data()) > 0) {
          payload = rs.getbytes(fux::tmq::f_data(), 0);
        }
      }
    }
    if (fail_diag != 0) {
      return remote_fail(ctl, "tpdequeue", fail_diag);
    }
    if (!payload.empty()) {
      if (tpimport(&payload[0], payload.size(), data, 0, 0) == -1) {
        return -1;
      }
    }
    if (len != nullptr) {
      *len = payload.size();
    }
    fux::atmi::reset_tperrno();
    return 0;
  }

  // the queue selectors (PEEK/GETBY/TQWAIT) travel in ctl->flags, the
  // pinned home; the flags argument carries API-level bits only
  return fux::tmq::exec_dequeue(qspace, qname, ctl, data, len, ctl->flags);
}
