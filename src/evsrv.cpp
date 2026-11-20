// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// Event server shared by the TMUSREVT and TMSYSEVT binaries. Owns the
// subscription database: a plain FML text file in APPDIR named after the
// binary (tmusrevt.dat / tmsysevt.dat), one record per change, appended
// and fsynced; state is derived by replaying newest-record-wins. The
// live table is the bulletin board's, so tppost/tpnotify deliver to
// subscribers without passing through this process; boot also compacts
// the file when tombstones outnumber live records.

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <atmi.h>
#include <fml32.h>
#include <tpadm.h>
#include <userlog.h>

#include "events.h"
#include "fux.h"
#include "mib.h"
#include "misc.h"
#include "tmqwire.h"

namespace {

constexpr uint64_t HANDLE_BASE = 1000000000ULL;
constexpr char EVT_SVC[] = ".TMEVT";

std::string g_datpath;
FILE *g_dat = nullptr;
uint64_t g_next_handle = HANDLE_BASE;
long g_seqno = 0;

std::string clientid_str(pid_t pid) {
  char buf[64];
  snprintf(buf, sizeof(buf), "0x%lx 0x0 0x2 0x0",
           static_cast<unsigned long>(pid));
  return buf;
}

void append_raw(const std::string &record) {
  fputs(record.c_str(), g_dat);
  fputc('\n', g_dat);
  fflush(g_dat);
  fsync(fileno(g_dat));
}

void append_version() {
  std::string s = "TA_SUBSCRIPTION_HANDLE\t";
  s += std::to_string(HANDLE_BASE);
  s += "\nTA_EB_DB_SEQNO\t" + std::to_string(g_seqno);
  s += "\nTA_CLASS\tEB_DB_VERSION\n";
  append_raw(s);
}

void append_subscribe(uint64_t handle, pid_t pid, const std::string &expr,
                      const std::string &filter) {
  std::string s = "TA_SUBSCRIPTION_HANDLE\t" + std::to_string(handle);
  s += "\nTA_CLASS\tT_EVENT_CLIENT";
  s += "\nTA_CLIENTID\t" + clientid_str(pid);
  s += "\nTA_EVENT_EXPR\t" + expr + "\n";
  if (!filter.empty()) {
    s += "TA_EVENT_FILTER\t" + filter + "\n";
  }
  append_raw(s);
}

void append_tombstone(uint64_t handle) {
  std::string s = "TA_SUBSCRIPTION_HANDLE\t" + std::to_string(handle);
  s += "\nTA_STATE\tINV";
  s += "\nTA_CLASS\tT_EVENT_CLIENT\n";
  append_raw(s);
}

int find_rpid(mib &m, pid_t pid) {
  for (size_t i = 0; i < m.accessers().length(); i++) {
    auto &a = m.accessers().at(i);
    if (a.valid() && a.pid == pid) {
      return a.rpid;
    }
  }
  return -1;
}

// The compiled tree is a binary blob whose first two bytes carry its
// length; a string copy would truncate at the first NUL. Returns false
// when the tree does not fit.
static bool copy_tree(const char *compiled, char (&tree)[1024]) {
  if (compiled == nullptr) {
    return false;
  }
  unsigned n = static_cast<unsigned char>(compiled[0]) |
               (static_cast<unsigned char>(compiled[1]) << 8);
  if (n < 4 || n + 1 > sizeof(tree)) {
    return false;
  }
  std::copy_n(compiled, n, tree);
  tree[n] = '\0';
  return true;
}

// Compile the filter expression once: the tree lives in the bulletin
// board entry (matched per post), the text goes to the .dat file.
// Fboolco32 returns an internal buffer, so copy immediately.
static bool compile_filter(const std::string &filter, char (&tree)[1024]) {
  tree[0] = '\0';
  if (filter.empty()) {
    return true;
  }
  return copy_tree(Fboolco32(const_cast<char *>(filter.c_str())), tree);
}

void compact(mib &m) {
  g_seqno++;
  std::string tmp = g_datpath + ".new";
  FILE *nf = fopen(tmp.c_str(), "w");
  if (nf == nullptr) {
    userlog("EVSVR: cannot compact %s: %s", tmp.c_str(), strerror(errno));
    return;
  }
  std::string out;
  {
    char buf[128];
    snprintf(buf, sizeof(buf), "TA_SUBSCRIPTION_HANDLE\t%llu\nTA_EB_DB_SEQNO\t%ld\nTA_CLASS\tEB_DB_VERSION\n\n",
             static_cast<unsigned long long>(HANDLE_BASE), g_seqno);
    out = buf;
  }
  for (size_t i = 0; i < m.events().len; i++) {
    auto &sub = m.events().at(i);
    if (!sub.used) {
      continue;
    }
    out += "TA_SUBSCRIPTION_HANDLE\t" + std::to_string(sub.handle);
    out += "\nTA_CLASS\tT_EVENT_CLIENT";
    out += "\nTA_CLIENTID\t" + clientid_str(sub.pid);
    out += "\nTA_EVENT_EXPR\t";
    out += sub.eventname;
    out += "\n";
    if (sub.filter[0] != '\0') {
      out += "TA_EVENT_FILTER\t";
      out += sub.filter;
      out += "\n";
    }
    out += "\n";
  }
  fwrite(out.data(), 1, out.size(), nf);
  fflush(nf);
  fsync(fileno(nf));
  fclose(nf);

  fclose(g_dat);
  if (rename(tmp.c_str(), g_datpath.c_str()) == -1) {
    userlog("EVSVR: cannot replace %s: %s", g_datpath.c_str(),
            strerror(errno));
  }
  g_dat = fopen(g_datpath.c_str(), "a");
  if (g_dat == nullptr) {
    userlog("EVSVR: cannot reopen %s: %s", g_datpath.c_str(),
            strerror(errno));
  }
}

void replay(mib &m) {
  uint64_t max_handle = HANDLE_BASE - 1;
  size_t inv = 0, dead = 0, live = 0;

  // The record format is fixed and small, so parse it directly instead
  // of going through field-table lookup: "FIELD\tvalue" lines, one blank
  // line between records. A torn append at a crash leaves a partial tail;
  // the stream simply ends there.
  auto process = [&](const std::vector<std::pair<std::string, std::string>>
                         &rec) {
    auto get = [&rec](const char *key) -> const char * {
      for (auto &kv : rec) {
        if (kv.first == key) {
          return kv.second.c_str();
        }
      }
      return nullptr;
    };
    const char *hs = get("TA_SUBSCRIPTION_HANDLE");
    if (hs == nullptr) {
      return;
    }
    uint64_t h = strtoull(hs, nullptr, 10);
    if (h > max_handle) {
      max_handle = h;
    }
    // the bulletin board survives a domain restart, so a record may
    // already be live from before the shutdown; keep it, do not copy
    if (m.events().find(h) != m.events().size) {
      return;
    }

    const char *cls = get("TA_CLASS");
    if (cls != nullptr && strcmp(cls, "EB_DB_VERSION") == 0) {
      const char *sq = get("TA_EB_DB_SEQNO");
      if (sq != nullptr) {
        g_seqno = strtol(sq, nullptr, 10);
      }
      return;
    }

    const char *state = get("TA_STATE");
    if (state != nullptr && strcmp(state, "INV") == 0) {
      inv++;
      auto pos = m.events().find(h);
      if (pos != m.events().size) {
        m.events().free(pos);
      }
      return;
    }

    const char *cid = get("TA_CLIENTID");
    const char *expr = get("TA_EVENT_EXPR");
    if (cid == nullptr || expr == nullptr) {
      return;
    }
    pid_t pid = static_cast<pid_t>(strtol(cid, nullptr, 16));
    if (kill(pid, 0) == -1 && errno == ESRCH) {
      dead++;
      return;
    }
    const char *flt = get("TA_EVENT_FILTER");

    try {
      size_t pos = m.events().alloc();
      auto &sub = m.events().at(pos);
      sub.handle = h;
      sub.pid = pid;
      sub.rpid = find_rpid(m, pid);
      sub.used = true;
      checked_copy(expr, sub.eventname);
      std::string filt = flt != nullptr ? flt : "";
      if (!compile_filter(filt, sub.filtertree)) {
        m.events().free(pos);
        userlog("EVSVR: filter does not compile, dropping handle %llu",
                static_cast<unsigned long long>(h));
        dead++;
        return;
      }
      checked_copy(filt, sub.filter);
      live++;
    } catch (const std::out_of_range &) {
      userlog("EVSVR: subscription table full, dropping handle %llu",
              static_cast<unsigned long long>(h));
    } catch (const std::length_error &) {
      userlog("EVSVR: dropping oversized record for handle %llu",
              static_cast<unsigned long long>(h));
    }
  };

  FILE *f = fopen(g_datpath.c_str(), "r");
  if (f != nullptr) {
    std::vector<std::pair<std::string, std::string>> rec;
    std::string line;
    auto flush = [&] {
      if (!rec.empty()) {
        process(rec);
        rec.clear();
      }
    };
    int c;
    while ((c = fgetc(f)) != EOF) {
      if (c == '\n') {
        if (line.empty()) {
          flush();
        } else {
          auto tab = line.find('\t');
          if (tab != std::string::npos) {
            rec.emplace_back(line.substr(0, tab), line.substr(tab + 1));
          }
          line.clear();
        }
      } else {
        line += static_cast<char>(c);
      }
    }
    flush();
    fclose(f);
  }

  g_next_handle = std::max(max_handle + 1, HANDLE_BASE);
  if (f == nullptr) {
    g_seqno = 1;
    append_version();
  } else if (inv + dead > live) {
    compact(m);
  }
}

void EVT(TPSVCINFO *svcinfo) {
  fux::fml32buf rq(svcinfo);
  fux::fml32buf rs;
  bool fail = false;
  try {
    auto &m = getmib();
    pid_t pid = static_cast<pid_t>(svcinfo->cltid.clientdata[0]);

    if (rq.count(fux::evw::f_data()) > 0) {
      // WS-routed post: match subscribers and deliver on the caller's
      // behalf (workstation clients have no bulletin board)
      std::string expr = rq.get<std::string>(TA_EVENT_EXPR, 0, "");
      std::string wire = rq.getbytes(fux::evw::f_data(), 0);
      char *buf = tpalloc(const_cast<char *>("CARRAY"), nullptr,
                          static_cast<long>(wire.size()) + 64);
      long olen = 0;
      if (!wire.empty() &&
          tpimport(&wire[0], static_cast<long>(wire.size()), &buf, &olen,
                   0) == -1) {
        TPERROR(TPEINVAL, "cannot import posted data");
        fail = true;
      } else {
        fail = fux::events::deliver(m, expr.c_str(),
                                    wire.empty() ? nullptr : buf, olen) == -1;
      }
      tpfree(buf);
      fux::tpreturn(fail ? TPFAIL : TPSUCCESS, 0, rs);
      return;
    }

    auto lock = m.data_lock();

    if (rq.count(TA_SUBSCRIPTION_HANDLE) > 0) {
      long h = rq.get<long>(TA_SUBSCRIPTION_HANDLE, 0);
      auto pos = m.events().find(static_cast<uint64_t>(h));
      if (pos == m.events().size) {
        TPERROR(TPEINVAL, "No such subscription %ld", h);
        fail = true;
      } else {
        m.events().free(pos);
        append_tombstone(static_cast<uint64_t>(h));
        rs.put(TA_SUBSCRIPTION_HANDLE, 0, h);
      }
    } else {
      std::string expr = rq.get<std::string>(TA_EVENT_EXPR, 0, "");
      if (expr.empty()) {
        TPERROR(TPEINVAL, "TA_EVENT_EXPR is required");
        fail = true;
      } else {
        std::string filter = rq.get<std::string>(TA_EVENT_FILTER, 0, "");
        uint64_t h = g_next_handle++;
        try {
          size_t pos = m.events().alloc();
          auto &sub = m.events().at(pos);
          sub.handle = h;
          sub.pid = pid;
          sub.rpid = find_rpid(m, pid);
          sub.used = true;
          checked_copy(expr, sub.eventname);
          if (!compile_filter(filter, sub.filtertree)) {
            m.events().free(pos);
            TPERROR(TPEINVAL, "filter expression does not compile");
            fail = true;
          }
          checked_copy(filter, sub.filter);
        } catch (const std::out_of_range &) {
          TPERROR(TPELIMIT, "Subscription table is full");
          fail = true;
        }
        if (!fail) {
          append_subscribe(h, pid, expr, filter);
          rs.put(TA_SUBSCRIPTION_HANDLE, 0, static_cast<long>(h));
        }
      }
    }
  } catch (const std::exception &e) {
    userlog("EVSVR: subscription update failed: %s", e.what());
    fail = true;
  }
  fux::tpreturn(fail ? TPFAIL : TPSUCCESS, 0, rs);
}

}  // namespace

namespace fux {
namespace evsrv {

// Called from the TMUSREVT/TMSYSEVT mains' tpsvrinit; the binary name
// selects the database file (tmusrevt.dat / tmsysevt.dat)
int init(int argc, char *argv[]) {
  std::string base = argc > 0 ? argv[0] : "tmusrevt";
  auto slash = base.find_last_of('/');
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  std::transform(base.begin(), base.end(), base.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  g_datpath = std::string(getmib().mach().appdir) + "/" + base + ".dat";
  g_dat = fopen(g_datpath.c_str(), "a");
  if (g_dat == nullptr) {
    userlog("EVSVR: cannot open %s: %s", g_datpath.c_str(), strerror(errno));
    return -1;
  }

  {
    auto &m = getmib();
    auto lock = m.data_lock();
    replay(m);
  }

  if (tpadvertise(const_cast<char *>(EVT_SVC), EVT) == -1) {
    userlog("EVSVR: failed to advertise %s: %s", EVT_SVC,
            tpstrerror(tperrno));
    return -1;
  }
  return 0;
}

void done() {
  if (g_dat != nullptr) {
    fclose(g_dat);
    g_dat = nullptr;
  }
}

}  // namespace evsrv
}  // namespace fux
