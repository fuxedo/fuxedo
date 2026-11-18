#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdint>
#include <string>
#include <vector>

#include <xa.h>

namespace fux::tlog {

enum class record_type : uint8_t {
  commit = 1,
  rollback = 2,
  // the branch completed without the transaction manager's decision
  // being logged (completed externally while in doubt); recovery treats
  // it as terminal, like the platform's heuristic completions
  heuristic = 3,
};

// A transaction log is a folder of fixed-size page files (like the /Q
// device). TLOGSIZE is the number of pages, TLOGOFFSET the first page
// number; both are kept in a meta file so the layout matches the platform
// semantics even though fuxedo grows pages on demand.
class log {
 public:
  // Creates the device: meta file with offset/size. Throws on error.
  static void create(const std::string &device, uint64_t offset,
                     uint64_t size);

  // Opens an existing device. Throws if the meta is missing.
  static log open(const std::string &device);

  uint64_t offset() const { return offset_; }
  uint64_t size() const { return size_; }

  // Appends a record and fsyncs the page. Pages are written circularly
  // inside [offset, offset + size).
  void append(record_type type, const XID *xid);

  // Scans the log backwards and returns the most recent outcome for the
  // gtrid part of xid, or 0 when there is none.
  int outcome(const XID *xid) const;

  // All gtrids with their latest outcome (for recovery and inspection).
  std::vector<std::pair<std::string, int>> recent() const;

 private:
  std::string device_;
  uint64_t offset_ = 0;
  uint64_t size_ = 0;
  uint64_t next_ = 0;

  std::string page_path(uint64_t page) const;
  uint64_t read_next() const;
  void write_next(uint64_t next) const;
};

}  // namespace fux::tlog
