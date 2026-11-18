// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <userlog.h>
#include <xa.h>
#include <xatmi.h>

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
#include "svcrepo.h"
#include "trace.h"
#include "trx.h"

#include "resp.h"
#include "svcrepo.h"
#include "trace.h"

#include <thread>

#include "client.h"
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

client::client(mib &mibcon)
    : mibcon_(mibcon), repo_(mibcon), blocktime_next(0), blocktime_all(0) {
  auto lock = mibcon_.data_lock();
  client_ = mibcon_.make_accesser(getpid());
  rpid = mibcon_.accessers().at(client_).rpid = fux::ipc::qcreate();
}

client::~client() {
  // Best-effort unregister: at process exit the application may
  // already be shut down and the bulletin board IPC gone; a throwing
  // destructor here would terminate the whole process.
  try {
    auto lock = mibcon_.data_lock();
    fux::ipc::qdelete(mibcon_.accessers().at(client_).rpid);
    mibcon_.accessers().at(client_).invalidate();
  } catch (const std::exception &) {
  }
}

int client::tpacall_group(int grpno, const char *svc, char *data, long len,
                          long flags) try {
  auto target = repo_.get_queue(grpno, svc);
  auto conv = repo_.buftypeconv(svc);

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
  if (data != nullptr) {
    if (tptypes(data, nullptr, nullptr) == -1) {
      return -1;
    }
  }

  rq.set_data(data, len);
  rq->mtype = target.mtype;
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
      mibcon_.accessers().at(client_).rpid_timeout =
          std::chrono::steady_clock::now() +
          std::chrono::milliseconds(next_blocktime());
      try {
        if (flags & TPNOBLOCK) {
          fux::ipc::qrecv(rpid, res, 0, IPC_NOWAIT);
        } else {
          fux::ipc::qrecv(rpid, res, 0, 0);
        }
      } catch (const std::system_error &) {
        mibcon_.accessers().at(client_).rpid_timeout = INVALID_TIME;
        TPERROR(TPEBLOCK, "No reply available");
        return -1;
      }
      mibcon_.accessers().at(client_).rpid_timeout = INVALID_TIME;
      userlog("%s received on %0x", __func__, rpid);
    }

    if (res->cat == fux::ipc::unblock) {
      if (flags & TPGETANY) {
        *cd = 0;
      } else {
        userlog("Received time-out for cd=%d", res->cd);
      }
      TPERROR(TPETIME, "Timeout message received");
      return -1;
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
  if (cds.release(cd) == -1) {
    return -1;
  }
  // TPETRAN
  fux::atmi::reset_tperrno();
  return 0;
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
  if (flags & TPNOTIME) {
    return fux::ipc::flags::notime;
  } else if (flags & TPNOBLOCK) {
    return fux::ipc::flags::noblock;
  } else {
    return fux::ipc::flags::noflags;
  }
}

static thread_local std::shared_ptr<client> current_client;
static client &getclient() {
  if (!current_client.get()) {
    current_client = std::make_shared<client>(getmib());
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
        getclient();
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
  if (fux::ws::ws_naddr() != nullptr) {
    return fux::atmi::exception_boundary(
        [&] { return fux::ws::get_wsconn().tpcancel(cd); }, -1);
  }
  return fux::atmi::exception_boundary([&] { return getclient().tpcancel(cd); },
                                       -1);
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
  return rc;
}
int tpcall(char *svc, char *idata, long ilen, char **odata, long *olen,
           long flags) {
  fux::trace::event("{ tpcall(\"%s\", %ld)", svc, ilen);
  int rc = fux::atmi::exception_boundary(
      [&] {
        fux::suspend_guard g(!(flags & TPNOTRAN));
        int cd = getclient().tpacall_group(-1, svc, idata, ilen, flags);
        if (cd == -1) {
          return -1;
        }
        return getclient().tpgetrply(&cd, odata, olen, flags);
      },
      -1);
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

int tppost(char * /*eventname*/, char * /*data*/, long /*len*/, long /*flags*/) {
  TPERROR(TPESYSTEM, "EventBroker not available");
  return -1;
}
