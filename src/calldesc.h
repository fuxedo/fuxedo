#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <thread>
#include <vector>

#include "misc.h"

namespace fux {

class call_descriptor_trx;

class call_descriptors {
 private:
  std::mutex mutex;
  std::vector<int> cds_;
  int seq_;

  // cycle 1..x
  int nextseq() {
    seq_++;
    if (seq_ > 0xffffff) {
      seq_ = 1;
    }
    return seq_;
  }

  void insert_cd(int cd) {
    cds_.insert(std::upper_bound(std::begin(cds_), std::end(cds_), cd), cd);
  }

  auto find_cd(int cd) {
    return std::upper_bound(std::begin(cds_), std::end(cds_), cd);
  }

  auto lock() { return fux::scoped_fuxlock(mutex); }

 public:
  call_descriptors() : seq_(0) {}

  call_descriptor_trx allocate();

  int release(int cd) {
    auto l = lock();
    auto it = find_cd(cd);
    if (it == cds_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    cds_.erase(it);
    return 0;
  }
};

class call_descriptor_trx {
 public:
  call_descriptor_trx(call_descriptors &cds, int cd)
      : cds_(cds), cd_(cd), released_(false) {}
  ~call_descriptor_trx() {
    if (!released_ && cd_ != -1) {
      cds_.release(cd_);
    }
  }
  void commit() { released_ = true; }
  int cd() { return cd_; }

 private:
  call_descriptors &cds_;
  int cd_;
  bool released_;
};

inline call_descriptor_trx call_descriptors::allocate() {
  auto l = lock();
  if (cds_.size() > 128) {
    TPERROR(TPELIMIT, "No free call descriptors");
    return call_descriptor_trx(*this, -1);
  }
  while (true) {
    auto cd = nextseq();
    if (find_cd(cd) == cds_.end()) {
      insert_cd(cd);
      return call_descriptor_trx(*this, cd);
    }
  }
}

}  // namespace fux
