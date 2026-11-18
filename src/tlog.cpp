// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "tlog.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

#include "misc.h"

namespace fux::tlog {

namespace fs = std::filesystem;

static const char *MAGIC = "FUXTLOG1";
static constexpr size_t PAGE_SIZE = 512;
static constexpr size_t XID_MAX = 64;

struct meta {
  char magic[8];
  uint64_t offset;
  uint64_t size;
  uint64_t next;
};

static std::string meta_path(const std::string &device) {
  return (fs::path(device) / ".tlog").string();
}

static std::string lock_path(const std::string &device) {
  return (fs::path(device) / ".lock").string();
}

class file_lock {
 public:
  explicit file_lock(const std::string &device) {
    for (int i = 0; i < 100; i++) {
      if (mkdir(lock_path(device).c_str(), 0700) == 0) {
        locked_ = true;
        return;
      }
      if (errno != EEXIST) {
        throw std::system_error(errno, std::system_category(),
                                "tlog lock");
      }
      struct stat st;
      if (stat(lock_path(device).c_str(), &st) == 0 &&
          time(nullptr) - st.st_mtime > 30) {
        rmdir(lock_path(device).c_str());
      }
      struct timespec ts = {0, 10 * 1000 * 1000};
      nanosleep(&ts, nullptr);
    }
    throw std::runtime_error("tlog lock timeout");
  }
  ~file_lock() {
    if (locked_) {
      rmdir(lock_path(device_).c_str());
    }
  }
  void device(const std::string &d) { device_ = d; }

 private:
  bool locked_ = false;
  std::string device_;
};

void log::create(const std::string &device, uint64_t offset, uint64_t size) {
  fs::create_directories(device);
  meta m = {};
  memcpy(m.magic, MAGIC, sizeof(m.magic));
  m.offset = offset;
  m.size = size;
  m.next = offset;

  std::ofstream out(meta_path(device), std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<char *>(&m), sizeof(m));
  out.flush();
  if (!out) {
    throw std::runtime_error("cannot write tlog meta");
  }
  int fd = ::open(meta_path(device).c_str(), O_WRONLY);
  if (fd != -1) {
    fsync(fd);
    close(fd);
  }
}

log log::open(const std::string &device) {
  std::ifstream in(meta_path(device), std::ios::binary);
  if (!in) {
    throw std::runtime_error(device + " is not a tlog device");
  }
  meta m = {};
  in.read(reinterpret_cast<char *>(&m), sizeof(m));
  if (!in || memcmp(m.magic, MAGIC, sizeof(m.magic)) != 0) {
    throw std::runtime_error(device + " has a bad tlog meta");
  }
  log l;
  l.device_ = device;
  l.offset_ = m.offset;
  l.size_ = m.size;
  l.next_ = m.next;
  return l;
}

log log::open_or_create(const std::string &device, uint64_t size) {
  std::error_code ec;
  if (fs::exists(meta_path(device), ec)) {
    return open(device);
  }
  create(device, 0, size == 0 ? 100 : size);
  return open(device);
}

std::string log::page_path(uint64_t page) const {
  char name[32];
  snprintf(name, sizeof name, "%08llu.page",
           static_cast<unsigned long long>(page));
  return (fs::path(device_) / name).string();
}

uint64_t log::read_next() const {
  std::ifstream in(meta_path(device_), std::ios::binary);
  meta m = {};
  in.read(reinterpret_cast<char *>(&m), sizeof(m));
  return m.next;
}

void log::write_next(uint64_t next) const {
  std::fstream f(meta_path(device_), std::ios::binary | std::ios::in |
                                         std::ios::out);
  f.seekp(offsetof(meta, next));
  f.write(reinterpret_cast<char *>(&next), sizeof(next));
  f.flush();
  int fd = ::open(meta_path(device_).c_str(), O_WRONLY);
  if (fd != -1) {
    fsync(fd);
    close(fd);
  }
}

void log::append(record_type type, const XID *xid) {
  if (xid->gtrid_length > static_cast<long>(XID_MAX)) {
    throw std::invalid_argument("gtrid too long for tlog record");
  }
  file_lock lock(device_);
  lock.device(device_);
  uint64_t page_no = read_next();

  std::vector<char> page(PAGE_SIZE, 0);
  {
    std::ifstream in(page_path(page_no), std::ios::binary);
    if (in) {
      in.read(page.data(), page.size());
    }
  }
  // find first free slot: records are [type:1][len:1][data:len]
  size_t pos = 0;
  while (pos + 2 < page.size()) {
    auto len = static_cast<size_t>(static_cast<uint8_t>(page[pos + 1]));
    if (page[pos] == 0 || len == 0 || pos + 2 + len > page.size()) {
      break;
    }
    pos += 2 + len;
  }
  size_t need = 2 + static_cast<size_t>(xid->gtrid_length);
  if (pos + need > page.size()) {
    // page full: move to the next page in the circular window and start
    // it empty - whatever was there belongs to an older lap
    page_no = offset_ + (page_no + 1 - offset_) % size_;
    std::fill(page.begin(), page.end(), 0);
    pos = 0;
  }

  page[pos] = static_cast<char>(type);
  page[pos + 1] = static_cast<char>(xid->gtrid_length);
  memcpy(page.data() + pos + 2, xid->data, xid->gtrid_length);

  std::string tmp = page_path(page_no) + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(page.data(), page.size());
    out.flush();
    int fd = ::open(tmp.c_str(), O_WRONLY);
    if (fd != -1) {
      fsync(fd);
      close(fd);
    }
  }
  rename(tmp.c_str(), page_path(page_no).c_str());

  write_next(page_no);
  next_ = page_no;
}

static bool page_records(const std::string &path,
                         std::map<std::string, int> &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::vector<char> page(PAGE_SIZE, 0);
  in.read(page.data(), page.size());

  size_t pos = 0;
  while (pos + 2 <= page.size()) {
    int type = static_cast<uint8_t>(page[pos]);
    auto len = static_cast<size_t>(static_cast<uint8_t>(page[pos + 1]));
    if (type == 0 || len == 0 || pos + 2 + len > page.size()) {
      break;
    }
    out[std::string(page.data() + pos + 2, len)] = type;
    pos += 2 + len;
  }
  return true;
}

int log::outcome(const XID *xid) const {
  auto all = recent();
  std::string g(reinterpret_cast<const char *>(xid->data),
                xid->gtrid_length);
  for (auto it = all.rbegin(); it != all.rend(); ++it) {
    if (it->first == g) {
      return it->second;
    }
  }
  return 0;
}

std::vector<std::pair<std::string, int>> log::recent() const {
  // Later pages carry newer records; within a page later slots are newer.
  // Replay in write order: oldest page first.
  std::map<std::string, int> merged;
  if (size_ == 0) {
    return {};
  }
  uint64_t newest = next_ == offset_ ? offset_ + size_ - 1 : next_ - 1;
  std::vector<std::string> paths;
  for (uint64_t i = 0; i < size_; i++) {
    uint64_t p = offset_ + (newest + size_ - i - offset_) % size_;
    std::map<std::string, int> recs;
    if (page_records(page_path(p), recs)) {
      for (auto &kv : recs) {
        if (merged.count(kv.first) == 0) {
          merged[kv.first] = kv.second;
        }
      }
    }
  }
  std::vector<std::pair<std::string, int>> out(merged.begin(), merged.end());
  std::reverse(out.begin(), out.end());
  return out;
}

}  // namespace fux::tlog
