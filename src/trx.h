#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <time.h>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include "defs.h"
#include "misc.h"

namespace fux {

namespace tx {
fux::gttid gttid();
bool own_begin();
void clear_own_begin();
bool transactional();
bool tx_active();
XID current_xid();
int tx_begin();
int tx_rollback();
}  // namespace tx
void tx_resume();
void tx_suspend();
void tx_join(fux::gttid id);
// Ends the current transaction (commit when ok, rollback otherwise);
// false when the requested outcome failed (e.g. commit was aborted)
bool tx_end(bool ok);
int tx_begin();

namespace tx {
extern uint16_t grpno;
extern size_t grpidx;
}  // namespace tx

namespace tm {
enum xa_func { prepare, commit, rollback };
// Drive the two-phase dance for xid against every group that joined it
// in the shared transaction table. Used by tx_commit/tx_rollback and by
// the domain gateway acting as a transaction subordinate. Returns an
// XA-style aggregate: XA_OK, XA_RDONLY when no branch needs a second
// phase, or the first failing branch's result.
int coordinate(const XID *xid, int op);
// The full two-phase sequence (prepare, decide, TLOG, second phase) for
// xid; true when committed. Used by tx_commit and by transaction agents
// (the workstation listener) that drive a transaction they adopted.
bool drive(const XID *xid);
}  // namespace tm

// Adopt an externally-originated transaction (domain gateway receiving
// a request inside a global transaction): find or create the shared
// table entry for xid and make it current for this thread. The caller
// owns no resource manager, so no xa_start runs. Pair with tx_detach().
fux::gttid tx_adopt(const XID &xid);
// Drop the transaction association from the current thread; the shared
// table entry stays alive for the commit protocol.
void tx_detach();

inline std::string to_string(XID *xid) {
  char buf[32];

  snprintf(buf, sizeof(buf), "%ld", xid->formatID);
  std::string out = buf;
  out += ".";

  unsigned char *data = reinterpret_cast<unsigned char *>(xid->data);
  for (int i = 0; i < xid->gtrid_length; i++) {
    snprintf(buf, sizeof(buf), "%02x", data[i]);
    out += buf;
  }
  if (xid->bqual_length > 0) {
    out += ".";
    for (int i = 0; i < xid->bqual_length; i++) {
      snprintf(buf, sizeof(buf), "%02x", data[xid->gtrid_length + i]);
      out += buf;
    }
  }

  return out;
}

inline XID to_xid(const std::string &s) {
  XID xid;
  auto parts = fux::split(s, ".");
  if (parts.size() != 2 && parts.size() != 3) {
    throw std::logic_error("Invalid XID string");
  }

  xid.formatID = std::stol(parts[0]);
  xid.gtrid_length = parts[1].size() / 2;
  if (parts.size() == 3) {
    xid.bqual_length = parts[2].size() / 2;
  } else {
    xid.bqual_length = 0;
  }

  unsigned char *data = reinterpret_cast<unsigned char *>(xid.data);
  for (int i = 0; i < xid.gtrid_length; i++) {
    data[i] = std::stoi(parts[1].substr(i * 2, 2), nullptr, 16);
  }
  for (int i = 0; i < xid.bqual_length; i++) {
    data[xid.gtrid_length + i] =
        std::stoi(parts[2].substr(i * 2, 2), nullptr, 16);
  }

  return xid;
}

}  // namespace fux

int _tx_suspend(TXINFO *info);
int _tx_resume(TXINFO *info);

struct transaction {
  XID xid;
  time_t started;
  time_t timeout;
  enum { active, preparing, do_commit, do_rollback, finished } state;
  char groups[];
};

struct transaction_table {
  size_t len;
  size_t size;
  size_t max_groups;
  size_t entry_size;
  struct transaction entry[];

  void init(size_t transactions, size_t groups) {
    len = 0;
    size = transactions;
    max_groups = groups;
    entry_size = size_of_transaction(groups);
  }

  transaction &operator[](size_t pos) {
    if (pos >= size) {
      throw std::out_of_range("index out of range");
    }
    return *reinterpret_cast<transaction *>(reinterpret_cast<char *>(entry) +
                                            (pos * entry_size));
  }

  static size_t size_of_transaction(size_t groups) {
    return nearest(
        (sizeof(transaction) + groups * sizeof(transaction::groups[0])),
        std::alignment_of<transaction>::value);
  }

  static size_t needed(size_t transactions, size_t groups) {
    return offsetof(transaction_table, entry) +
           transactions * size_of_transaction(groups);
  }

  transaction &join(uint16_t transaction, uint16_t group) {
    if (group >= max_groups) {
      throw std::out_of_range("group out of range");
    }
    auto &t = (*this)[transaction];
    t.groups[group] = 1;
    return t;
  }

  bool has_joined(size_t transaction, size_t group) {
    if (group >= max_groups) {
      throw std::out_of_range("group out of range");
    }
    auto &t = (*this)[transaction];
    return t.groups[group] > 0;
  }

  fux::gttid init(fux::gttid i, const XID &xid, uint16_t group) {
    memset(&(*this)[i], 0, entry_size);
    (*this)[i].xid = xid;
    join(i, group);
    return i;
  }

  fux::gttid start(const XID &xid, uint16_t group) {
    for (size_t i = 0; i < len; i++) {
      if ((*this)[i].xid.formatID == -1) {
        return init(i, xid, group);
      }
    }
    if (len < size) {
      return init(len++, xid, group);
    }
    throw std::out_of_range("No free entries in transaction table");
  }

  // entry without joining a group: the domain gateway adopts incoming
  // transactions on behalf of remote coordinators and owns no local
  // branch itself; the branches join when local servers do the work
  fux::gttid start(const XID &xid) {
    for (size_t i = 0; i < len; i++) {
      if ((*this)[i].xid.formatID == -1) {
        memset(&(*this)[i], 0, entry_size);
        (*this)[i].xid = xid;
        (*this)[i].xid.formatID = xid.formatID;
        return i;
      }
    }
    if (len < size) {
      auto i = len++;
      memset(&(*this)[i], 0, entry_size);
      (*this)[i].xid = xid;
      return i;
    }
    throw std::out_of_range("No free entries in transaction table");
  }

  fux::gttid find(XID &xid) {
    for (size_t i = 0; i < len; i++) {
      XID &x = (*this)[i].xid;
      if (x.formatID == xid.formatID && x.gtrid_length == xid.gtrid_length &&
          x.bqual_length == xid.bqual_length &&
          memcmp(x.data, xid.data, xid.gtrid_length + xid.bqual_length) == 0) {
        return i;
      }
    }
    throw std::out_of_range("Not found in transaction table");
  }

  transaction &at(fux::gttid i) {
    if (i < len) {
      return (*this)[i];
    }
    throw std::out_of_range("Transaction not found");
  }

  void end(size_t transaction) {
    auto &t = (*this)[transaction];
    memset(&t, 0, entry_size);
    t.xid.formatID = -1;
  }
};
