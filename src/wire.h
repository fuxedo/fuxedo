#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Big-endian serializer for the link and workstation frame payloads;
// appends into an existing vector so a frame can be built in place.
namespace fux::wire {

class writer {
 public:
  explicit writer(std::vector<char> &out) : b(out) {}
  void u16(uint16_t v) {
    v = htons(v);
    const char *p = reinterpret_cast<const char *>(&v);
    b.insert(b.end(), p, p + sizeof(v));
  }
  void u32(uint32_t v) {
    v = htonl(v);
    const char *p = reinterpret_cast<const char *>(&v);
    b.insert(b.end(), p, p + sizeof(v));
  }
  void u64(uint64_t v) {
    u32(static_cast<uint32_t>(v >> 32));
    u32(static_cast<uint32_t>(v & 0xffffffffu));
  }
  void str(const std::string &s) {
    u16(static_cast<uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
  }
  void bytes(const void *p, size_t n) {
    u32(static_cast<uint32_t>(n));
    const char *c = static_cast<const char *>(p);
    b.insert(b.end(), c, c + n);
  }

 private:
  std::vector<char> &b;
};

class reader {
 public:
  reader(const std::vector<char> &v) : p(v.data()), n(v.size()) {}
  bool u16(uint16_t &v) {
    if (off + 2 > n) return false;
    memcpy(&v, p + off, 2);
    v = ntohs(v);
    off += 2;
    return true;
  }
  bool u32(uint32_t &v) {
    if (off + 4 > n) return false;
    memcpy(&v, p + off, 4);
    v = ntohl(v);
    off += 4;
    return true;
  }
  bool u64(uint64_t &v) {
    uint32_t a, b;
    if (!u32(a) || !u32(b)) return false;
    v = (static_cast<uint64_t>(a) << 32) | b;
    return true;
  }
  bool str(std::string &s) {
    uint16_t len;
    if (!u16(len) || off + len > n) return false;
    s.assign(p + off, len);
    off += len;
    return true;
  }
  bool bytes(std::vector<char> &v) {
    uint32_t len;
    if (!u32(len) || off + len > n) return false;
    v.assign(p + off, p + off + len);
    off += len;
    return true;
  }

 private:
  const char *p;
  size_t n;
  size_t off = 0;
};

}  // namespace fux::wire
