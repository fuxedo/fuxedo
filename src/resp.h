// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

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
    auto cd = nextseq();
    if (calls_.find(cd) != calls_.end()) {
      // If cd wrapped around and cd has still no response
      // assume it will never come
    }
    calls_[cd] = response{flags::none, {}};
    return cd;
  }

  int check(int cd) {
    auto it = calls_.find(cd);
    if (it == calls_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    return 0;
  }

  int any_buffered() {
    for (auto it : calls_) {
      if (it.second.status == flags::buffered) {
        return it.first;
      }
    }
    return -1;
  }

  void buffer(fux::ipc::msg &&res) {
    calls_[res->cd].res = res;
    calls_[res->cd].status = flags::buffered;
  }

  bool is_buffered(int cd) { return calls_[cd].status == flags::buffered; }

  fux::ipc::msg &buffered(int cd) { return calls_[cd].res; }

  int release(int cd) {
    auto it = calls_.find(cd);
    if (it == calls_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    calls_.erase(it);
    return 0;
  }
};
