#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <time.h>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include "misc.h"

namespace fux {

static const long FORMAT_ID = 0xda7aba5e;

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
  out += ".";
  for (int i = 0; i < xid->bqual_length; i++) {
    snprintf(buf, sizeof(buf), "%02x", data[xid->gtrid_length + i]);
    out += buf;
  }

  return out;
}

inline XID to_xid(const std::string &s) {
  XID xid;
  auto parts = fux::split(s, ".");
  if (parts.size() != 3) {
    throw std::logic_error("XID must consist of 3 parts");
  }

  xid.formatID = std::stol(parts[0]);
  xid.gtrid_length = parts[1].size() / 2;
  xid.bqual_length = parts[2].size() / 2;

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

struct transaction {
  fux::trxid id;
  time_t started;
  time_t timeout;
  enum { active, preparing, do_commit, do_rollback, finished } state;
  uint8_t participants[];
};

struct transaction_table {
  size_t len;
  size_t size;
  size_t max_participants;
  size_t entry_size;
  struct transaction entry[];

  void init(size_t transactions, size_t participants) {
    len = 0;
    size = transactions;
    max_participants = participants;
    entry_size = size_of_transaction(participants);
  }

  transaction &operator[](size_t pos) {
    if (pos >= size) {
      throw std::out_of_range("index out of range");
    }
    return *reinterpret_cast<transaction *>(reinterpret_cast<char *>(entry) +
                                            (pos * entry_size));
  }

  static size_t size_of_transaction(size_t participants) {
    return nearest((sizeof(transaction) +
                    participants * sizeof(transaction::participants[0])),
                   std::alignment_of<transaction>::value);
  }

  static size_t needed(size_t transactions, size_t participants) {
    return offsetof(transaction_table, entry) +
           transactions * size_of_transaction(participants);
  }

  void join(size_t transaction, size_t participant) {
    if (participant >= max_participants) {
      throw std::out_of_range("participant out of range");
    }
    auto &t = (*this)[transaction];
    t.participants[participant] = 1;
  }

  bool has_joined(size_t transaction, size_t participant) {
    if (participant >= max_participants) {
      throw std::out_of_range("participant out of range");
    }
    auto &t = (*this)[transaction];
    return t.participants[participant] > 0;
  }

  size_t start(fux::trxid id) {
    for (size_t i = 0; i < len; i++) {
      if ((*this)[i].id == 0) {
        memset(&(*this)[i], 0, entry_size);
        (*this)[i].id = id;
        return i;
      }
    }
    if (len < size) {
      memset(&(*this)[len], 0, entry_size);
      (*this)[len].id = id;
      return len++;
    }
    throw std::out_of_range("No free entries in transaction table");
  }

  void end(size_t transaction) {
    auto &t = (*this)[transaction];
    memset(&t, 0, entry_size);
  }
};
