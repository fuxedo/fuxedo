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

#include <xatmi.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>

#define TPERROR(err, fmt, args...)                                       \
  fux::atmi::set_tperrno(err, "%s() in %s:%d: " fmt, __func__, __FILE__, \
                         __LINE__, ##args)

namespace fux {
namespace util {
inline std::string getenv(const char *name, const char *defval) {
  auto *val = std::getenv(name);
  if (val != nullptr) {
    return val;
  }
  return defval;
}
}
}

namespace fux {
namespace atmi {
void set_tperrno(int err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void reset_tperrno();
}
}

#define FERROR32(err, fmt, args...)                                        \
  fux::fml32::set_Ferror32(err, "%s() in %s:%d: " fmt, __func__, __FILE__, \
                           __LINE__, ##args)
namespace fux {
namespace fml32 {
void set_Ferror32(int err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void reset_Ferror32();
}
}

template <unsigned int N>
void checked_copy(const std::string &src, char (&dst)[N]) {
  if (src.size() >= N) {
    throw std::length_error(src + " too long");
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
  for (const std::string &s : container) {
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
}
}

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
}

namespace fux {
typedef uint64_t trxid;
}

static constexpr size_t nearest(size_t n, size_t what, size_t mult = 1) {
  return n <= (mult * what) ? (mult * what) : nearest(n, what, mult + 1);
}

static constexpr size_t nearest64(size_t n) { return nearest(n, 64); }
