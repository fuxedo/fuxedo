#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdint>
#include <cstddef>
#include <sys/types.h>

#include "misc.h"

// Event subscription table: lives in bulletin board shared memory so
// tppost/tpnotify can match subscribers and push straight into their
// unsolicited channel (category unsol) without a service hop through
// the event server. The event server (TMUSREVT/TMSYSEVT) owns the
// on-disk copy: a plain FML text file appended per subscription change.

struct event_subscription {
  uint64_t handle;  // 1000000000+, like the platform
  pid_t pid;        // subscriber process, liveness-checked on post
  int rpid;         // subscriber unsolicited queue id
  bool used;
  char eventname[128];
  char filter[256];
};

struct event_table {
  size_t len;
  size_t size;
  struct event_subscription entry[];

  void init(size_t n) {
    len = 0;
    size = n;
  }

  static size_t needed(size_t n) {
    return nearest64(offsetof(event_table, entry) +
                     n * sizeof(event_subscription));
  }

  event_subscription &at(size_t pos) { return entry[pos]; }

  // Free slot scan, then append. Throws when the table is full.
  size_t alloc() {
    for (size_t i = 0; i < len; i++) {
      if (!entry[i].used) {
        return i;
      }
    }
    if (len < size) {
      return len++;
    }
    throw std::out_of_range("No free entries in event subscription table");
  }

  void free(size_t pos) { entry[pos].used = false; }

  // Newest-wins lookup by handle; badoff-style npos when absent.
  size_t find(uint64_t handle) {
    for (size_t i = 0; i < len; i++) {
      if (entry[i].used && entry[i].handle == handle) {
        return i;
      }
    }
    return size;  // npos
  }
};
