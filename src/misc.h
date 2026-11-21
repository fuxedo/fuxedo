#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xatmi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/types.h>

std::string string_format(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#define FERROR(err, fmt, args...)                                          \
  fux::fml32::set_Ferror32(err, "%s() in %s:%d: " fmt, __func__, __FILE__, \
                           __LINE__, ##args)

#define TPERROR(err, fmt, args...)                                       \
  fux::atmi::set_tperrno(err, "%s() in %s:%d: " fmt, __func__, __FILE__, \
                         __LINE__, ##args)

namespace fux::fml32 {
void set_Ferror32(int err, const char *fmt, ...);
void reset_Ferror32();

template <typename F>
void exception_boundary(F &&f) noexcept {
  try {
    reset_Ferror32();
    f();
  } catch (const std::exception &e) {
    FERROR(-1, "Unhandled exception [%s]", e.what());
  }
}

template <typename F, typename R>
auto exception_boundary(F &&f, R &&r) noexcept -> decltype(f()) {
  try {
    reset_Ferror32();
    return f();
  } catch (const std::exception &e) {
    FERROR(-1, "Unhandled exception [%s]", e.what());
    return r;
  }
}
}  // namespace fux::fml32

namespace fux::util {
inline std::string getenv(const char *name, const char *defval) {
  auto *val = std::getenv(name);
  if (val != nullptr) {
    return val;
  }
  return defval;
}
}  // namespace fux::util

namespace fux::atmi {
void set_tperrno(int err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void reset_tperrno();

template <typename F>
void exception_boundary(F &&f) noexcept {
  try {
    // no tperrno reset at entry: the platform leaves tperrno stale on
    // most calls (probed: tpalloc, tpstrerror, the whole FML32 family)
    // and only the buffer-management family resets it - those set it
    // explicitly on their success paths
    f();
  } catch (const std::exception &e) {
    // an internal failure surfaces as TPESYSTEM, the platform's answer
    // for calls made without a usable context (probed: Oracle 12.2.2
    // returns TPESYSTEM for tp* calls issued before tpinit)
    TPERROR(TPESYSTEM, "Unhandled exception [%s]", e.what());
  }
}

template <typename F, typename R>
auto exception_boundary(F &&f, R &&r) noexcept -> decltype(f()) {
  try {
    return f();
  } catch (const std::exception &e) {
    TPERROR(TPESYSTEM, "Unhandled exception [%s]", e.what());
    return r;
  }
}

}  // namespace fux::atmi

template <unsigned int N>
void checked_copy(const std::string &src, char (&dst)[N]) {
  if (src.size() >= N) {
    throw std::length_error(string_format(
        "%s too long for destination of %d bytes", src.c_str(), N));
  }
  std::copy_n(src.c_str(), src.size(), dst);
}

template <unsigned int N>
void checked_copy(const char *src, char (&dst)[N]) {
  auto size = strlen(src) + 1;
  if (size > N) {
    throw std::length_error(std::string(src) + " too long");
  }
  std::copy_n(src, size, dst);
}

template <typename T>
std::string join(const T &container, const std::string &sep) {
  std::string result;
  for (const auto &s : container) {
    if (!result.empty()) {
      result.append(sep);
    }
    result.append(s);
  }
  return result;
}

size_t base64encode(const void *ibuf, size_t ilen, char *obuf, size_t olen);
size_t base64decode(const char *ibuf, size_t ilen, void *obuf, size_t olen);

constexpr size_t base64chars(size_t ilen) {
  auto blocks = ilen / 3;
  auto needed = blocks * 4;
  if (blocks * 3 < ilen) {
    needed += 4;
  }
  return needed;
}

struct cmp_cstr {
  bool operator()(char const *a, char const *b) const {
    return std::strcmp(a, b) < 0;
  }
};

namespace fux {
namespace mem {
void setowner(char *ptr, char **owner);
long bufsize(char *ptr, long used = -1);

// Size of the typed-buffer bookkeeping prefix that bufsize() includes;
// data-only length is bufsize(ptr, -1) - headersize().
long headersize();

// Size of the allocated data area, regardless of content.
long allocsize(char *ptr);
}  // namespace mem
}  // namespace fux

namespace fux {
extern bool threaded;

class scoped_fuxlock {
 public:
  scoped_fuxlock(std::mutex &m) : m_(m) {
    if (threaded) {
      m_.lock();
    }
  }
  ~scoped_fuxlock() {
    if (threaded) {
      m_.unlock();
    }
  }

  scoped_fuxlock(scoped_fuxlock &&) = default;
  scoped_fuxlock &operator=(scoped_fuxlock &&) = default;

 private:
  std::mutex &m_;
  scoped_fuxlock(const scoped_fuxlock &) = delete;
  scoped_fuxlock &operator=(const scoped_fuxlock &) = delete;
};

inline std::vector<std::string> split(const std::string &s,
                                      const std::string &delim) {
  std::vector<std::string> tokens;
  std::string token;

  for (auto const c : s) {
    if (delim.find(c) == std::string::npos) {
      token += c;
    } else {
      tokens.push_back(token);
      token.clear();
    }
  }
  tokens.push_back(token);
  return tokens;
}
}  // namespace fux

static constexpr size_t nearest(size_t n, size_t what, size_t mult = 1) {
  return n <= (mult * what) ? (mult * what) : nearest(n, what, mult + 1);
}

static constexpr size_t nearest64(size_t n) { return nearest(n, 64); }

// kill(pid, 0) succeeds for zombies, which are as good as dead here: a
// dead child not yet reaped would otherwise look alive forever.
inline bool alive(pid_t pid) {
  if (kill(pid, 0) == -1) {
    return false;
  }
  std::string stat = "/proc/" + std::to_string(pid) + "/stat";
  std::ifstream in(stat);
  if (!in) {
    return true;  // not Linux or gone mid-check; trust kill(2)
  }
  std::string line;
  std::getline(in, line);
  auto close_paren = line.rfind(')');
  if (close_paren == std::string::npos) {
    return true;
  }
  return close_paren + 2 >= line.size() || line[close_paren + 2] != 'Z';
}
