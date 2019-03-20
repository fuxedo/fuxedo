#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <time.h>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include "misc.h"

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
    return nearest((sizeof(transaction) + participants / 8 + 1),
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
    t.participants[participant / 8] |= 1 << (participant % 8);
  }

  bool has_joined(size_t transaction, size_t participant) {
    if (participant >= max_participants) {
      throw std::out_of_range("participant out of range");
    }
    auto &t = (*this)[transaction];
    return t.participants[participant / 8] & 1 << (participant % 8);
  }

  size_t start(fux::trxid id) {
    for (size_t i = 0; i < len; i++) {
      if ((*this)[i].id == 0) {
        (*this)[i].id = id;
        return i;
      }
    }
    if (len < size) {
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
