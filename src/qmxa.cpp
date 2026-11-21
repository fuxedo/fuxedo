// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <userlog.h>
#include <xa.h>

#ifdef HAVE_CXX_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#include "misc.h"
#include <fcntl.h>
#include <unistd.h>

#include "probes.h"
#include "trx.h"

// XA resource manager for the file-based /Q device. Operations performed
// under a global transaction are staged in
//   <device>/<qspace>/.tx/<gtrid>/
// as enq_<queue>_<msgid> / deq_<queue>_<msgid> files. prepare fsyncs the
// staging area; commit applies the staged effects; rollback undoes them.
// After a crash, staged directories left behind are in-doubt branches and
// are resolved by the recovery scan against the TLOG.

static fs::path g_base;

static fs::path txdir(const XID *xid) {
  XID gtrid = *xid;
  gtrid.bqual_length = 0;
  return g_base / ".tx" / fux::to_string(&gtrid);
}

static int xa_open(char *xa_info, int, long) {
  if (xa_info == nullptr) {
    return TMER_INVAL;
  }
  FUX_PROBE1(qm__open, xa_info);
  auto parts = fux::split(xa_info, ":");
  if (parts.size() != 2) {
    userlog("Require DEVICE:QSPACE in OPENINFO");
    return XAER_INVAL;
  }
  if (!fs::is_directory(parts[0])) {
    userlog("DEVICE %s must be a directory", parts[0].c_str());
    return XAER_INVAL;
  }
  g_base = fs::path(parts[0]) / parts[1];
  if (!fs::is_directory(g_base)) {
    userlog("QSPACE %s must exist under %s", parts[1].c_str(),
            parts[0].c_str());
    return XAER_INVAL;
  }
  return XA_OK;
}

static int xa_close(char *, int, long) { return XA_OK; }
static int xa_start(XID *, int, long) { return XA_OK; }
static int xa_end(XID *, int, long) { return XA_OK; }

static int xa_prepare(XID *xid, int, long) {
  auto gtrid = fux::to_string(xid);
  FUX_PROBE1(qm__prepare, gtrid.c_str());
  auto dir = txdir(xid);
  if (!fs::is_directory(dir)) {
    return XA_RDONLY;
  }
  if (fs::is_empty(dir)) {
    return XA_RDONLY;
  }
  // fsync staged files so a commit decision can be replayed after a crash
  for (auto &p : fs::directory_iterator(dir)) {
    int fd = ::open(p.path().c_str(), O_RDONLY);
    if (fd != -1) {
      fsync(fd);
      close(fd);
    }
  }
  FUX_PROBE1(qm__prepare_return, XA_OK);
  return XA_OK;
}

// Applies or undoes the staged effects. Returns false when the staging
// directory is missing (nothing to do).
static bool apply(XID *xid, bool commit) {
  auto dir = txdir(xid);
  if (!fs::is_directory(dir)) {
    return false;
  }
  for (auto &p : fs::directory_iterator(dir)) {
    try {
      auto name = p.path().filename().string();
      if (name.rfind("enq_", 0) == 0) {
        auto rest = name.substr(4);
        auto sep = rest.find('_');
        auto target = g_base / rest.substr(0, sep) / rest.substr(sep + 1);
        if (commit) {
          fs::rename(p.path(), target);
        } else {
          fs::remove(p.path());
        }
      } else if (name.rfind("deq_", 0) == 0) {
        auto rest = name.substr(4);
        auto sep = rest.find('_');
        auto home = g_base / rest.substr(0, sep) / rest.substr(sep + 1);
        if (commit) {
          fs::remove(p.path());
        } else {
          fs::rename(p.path(), home);
        }
      }
    } catch (const fs::filesystem_error &) {
      // a concurrent recovery may have applied the same effect; both
      // commit and rollback are idempotent here
    }
  }
  fs::remove_all(dir);
  return true;
}

namespace fux::qm {
bool qm_branch_staged_empty(const XID *xid) {
  auto dir = txdir(xid);
  std::error_code ec;
  return fs::is_directory(dir, ec) && fs::is_empty(dir, ec);
}
}  // namespace fux::qm

static int xa_commit(XID *xid, int, long) {
  FUX_PROBE1(qm__commit, fux::to_string(xid).c_str());
  apply(xid, true);
  return XA_OK;
}

static int xa_rollback(XID *xid, int, long) {
  FUX_PROBE1(qm__rollback, fux::to_string(xid).c_str());
  apply(xid, false);
  return XA_OK;
}

static int xa_complete(int *, int *, int, long) { return XA_OK; }
static int xa_forget(XID *, int, long) { return XA_OK; }

static int xa_recover(XID *xids, long count, int, long) {
  // staged directories are the in-doubt branches of this resource
  auto txroot = g_base / ".tx";
  if (!fs::is_directory(txroot)) {
    return 0;
  }
  int n = 0;
  for (auto &p : fs::directory_iterator(txroot)) {
    if (!fs::is_directory(p.path()) || n >= count) {
      continue;
    }
    try {
      xids[n++] = fux::to_xid(p.path().filename().string());
    } catch (const std::exception &) {
      userlog("skipping unparseable tlog staging dir %s",
              p.path().filename().string().c_str());
    }
  }
  FUX_PROBE1(qm__recover, n);
  return n;
}

extern "C" {
struct xa_switch_t tuxq_switch {
  "TUXEDO/QM", TMNOFLAGS, 0, xa_open, xa_close, xa_start, xa_end, xa_rollback,
      xa_prepare, xa_commit, xa_recover, xa_forget, xa_complete
};
}
