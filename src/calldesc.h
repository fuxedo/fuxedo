#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <thread>
#include <vector>

#include "misc.h"

namespace fux {

class call_describtors {
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

 public:
  call_describtors() : seq_(0) {}

  auto lock() { return fux::scoped_fuxlock(mutex); }

  int allocate() {
    if (cds_.size() > 128) {
      TPERROR(TPELIMIT, "No free call descriptors");
      return -1;
    }
    while (true) {
      auto cd = nextseq();
      if (find_cd(cd) == cds_.end()) {
        insert_cd(cd);
        return cd;
      }
    }
  }

  int release(int cd) {
    auto it = find_cd(cd);
    if (it == cds_.end()) {
      TPERROR(TPEBADDESC, "Not allocated");
      return -1;
    }
    cds_.erase(it);
    return 0;
  }
};
}
