// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#pragma once

#include <set>

#include "ipc.h"
#include "mib.h"
#include "misc.h"

class responses {
 private:
  enum struct flags : int { none = 0, buffered = 1 };
  struct response {
    flags status;
    fux::ipc::msg res;
  };
  std::map<int, response> calls_;
  // descriptors whose caller already gave up (timeout or cancel): the
  // number stays out of circulation until the service's late reply
  // arrives and is discarded, so a stale reply can never satisfy a
  // different call that reused the descriptor
  std::set<int> stale_;
  int seq_;

  // cycle [1..INT_MAX]
  // 0 is reserved as cd for tpacall(..., TPNOREPLY)
  int nextseq() {
    seq_++;
    if (seq_ == std::numeric_limits<int>::max()) {
      seq_ = 1;
    }
    return seq_;
  }

 public:
  responses() : seq_(0) {}

  int allocate() {
    // Tuxedo has larger messages, increase limits to have similar test results
    // if (calls_.size() > 128) {
    if (calls_.size() > 1024) {
      TPERROR(TPELIMIT, "No free call descriptors");
      return -1;
    }
    for (int attempts = 0; attempts < 100000; attempts++) {
      auto cd = nextseq();
      if (calls_.count(cd) == 0 && stale_.count(cd) == 0) {
        calls_[cd] = response{flags::none, {}};
        return cd;
      }
    }
    TPERROR(TPELIMIT, "No free call descriptors");
    return -1;
  }

  int check(int cd) {
    if (stale_.count(cd) != 0) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    auto it = calls_.find(cd);
    if (it == calls_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    return 0;
  }

  // the caller gave up on cd (timeout or cancel); protect its number
  // from reuse until the late reply, if any, is discarded
  void mark_stale(int cd) {
    calls_.erase(cd);
    if (stale_.size() > 4096) {
      // a reply that never arrives must not exhaust the descriptor
      // space; drop the oldest protection
      userlog("responses: dropping stale reply protection for cd=%d",
              *stale_.begin());
      stale_.erase(stale_.begin());
    }
    stale_.insert(cd);
  }

  bool is_stale(int cd) const { return stale_.count(cd) != 0; }

  // a late reply arrived: discard it and return the descriptor to
  // circulation
  void discard_late(int cd) { stale_.erase(cd); }

  int any_buffered() {
    for (auto it : calls_) {
      if (it.second.status == flags::buffered) {
        return it.first;
      }
    }
    return -1;
  }

  bool empty() const { return calls_.empty(); }

  void buffer(fux::ipc::msg &&res) {
    calls_[res->cd].res = res;
    calls_[res->cd].status = flags::buffered;
  }

  bool is_buffered(int cd) { return calls_[cd].status == flags::buffered; }

  fux::ipc::msg &buffered(int cd) { return calls_[cd].res; }

  int release(int cd) {
    stale_.erase(cd);
    auto it = calls_.find(cd);
    if (it == calls_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    calls_.erase(it);
    return 0;
  }
};
