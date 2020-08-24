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
#include "trx.h"

// int ax_reg(int, XID *, long) { return TMER_TMERR; }
// int ax_unreg(int, long) { return TMER_TMERR; }

struct qspace {
  fs::path base;
};

static thread_local qspace qs;

static int xa_open(char *xa_info, int, long) {
  if (xa_info == nullptr) {
    return TMER_INVAL;
  }
  auto parts = fux::split(xa_info, ":");
  if (parts.size() != 2) {
    userlog("Require DEVICE:QSPACE in OPENINFO");
    return TMER_INVAL;
  }

  std::string device = parts[0];
  std::string qspace = parts[1];

  if (!fs::is_directory(device)) {
    userlog("DEVICE must be a directory");
    return TMER_INVAL;
  }
  if (!fs::is_directory(fs::path(device) / qspace)) {
    userlog("QSPACE must be a directory under DEVICE");
    return TMER_INVAL;
  }

  qs.base = fs::path(device) / qspace;
  return XA_OK;
}

static std::string path(XID *xid) {
  XID gtrid = *xid;
  gtrid.bqual_length = 0;
  return fux::to_string(&gtrid);
}

static int xa_close(char *, int, long) { return XA_OK; }
static int xa_start(XID *xid, int, long) {
  auto dir = path(xid);

  if (!fs::is_directory(dir)) {
    fs::create_directory(dir);
  }

  return XA_OK;
}

static void publish(XID *xid, bool commit) {
  auto dir = path(xid);
  if (!fs::is_directory(dir)) {
    dir += ".prepared";
  }

  for (auto &p : fs::directory_iterator(dir)) {
    auto filename = p.path().filename().string();
    if (filename[0] == '-') {
    } else if (filename[0] == '+') {
    }
  }

  fs::remove_all(dir);
}

static int xa_rollback(XID *xid, int, long) {
  publish(xid, false);
  return XA_OK;
}

static int xa_commit(XID *xid, int, long) {
  publish(xid, true);
  return XA_OK;
}

static int xa_complete(int *, int *, int, long) { return XA_OK; }
static int xa_end(XID *, int, long) { return XA_OK; }
static int xa_forget(XID *, int, long) { return XA_OK; }

static int xa_prepare(XID *xid, int, long) {
  auto dir = path(xid);
  if (fs::is_empty(dir)) {
    return XA_RDONLY;
  }
  fs::rename(dir, dir + ".prepared");
  return XA_OK;
}

static int xa_recover(XID *, long, int, long) { return XA_OK; }
extern "C" {
struct xa_switch_t tuxq_switch {
  "TUXEDO/QM", TMNOFLAGS, 0, xa_open, xa_close, xa_start, xa_end, xa_rollback,
      xa_prepare, xa_commit, xa_recover, xa_forget, xa_complete
};
}
