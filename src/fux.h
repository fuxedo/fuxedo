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

#include <fml32.h>
#include <xatmi.h>

#include <functional>
#include <stdexcept>
#include <string>

namespace fux {

class fml32buf_error : public std::exception {
 public:
  fml32buf_error() : code_(Ferror32) {}
  virtual const char *what() const noexcept override {
    return Fstrerror32(code_);
  }

 private:
  long code_;
};

class xatmi_error : public std::exception {
 public:
  xatmi_error() : code_(tperrno) {}
  virtual const char *what() const noexcept override {
    return tpstrerror(code_);
  }

 private:
  long code_;
};

template <typename T>
struct identity {
  typedef T type;
};

class fml32buf {
 public:
  fml32buf() : fbfr_(&owned_) {
    owned_ = reinterpret_cast<FBFR32 *>(
        tpalloc(const_cast<char *>("FML32"), nullptr, 1024));
  }
  fml32buf(TPSVCINFO *svcinfo)
      : fbfr_(reinterpret_cast<FBFR32 **>(&svcinfo->data)), owned_(nullptr) {}
  fml32buf(FBFR32 **owning_ptr)
      : fbfr_(owning_ptr), owned_(nullptr) {}

  fml32buf(const fml32buf &) = delete;
  fml32buf(fml32buf &&) = delete;
  fml32buf &operator=(const fml32buf &) = delete;
  fml32buf &operator=(fml32buf &&) = delete;

  template <typename T>
  T get(FLDID32 fieldid, FLDOCC32 oc) {
    return get(fieldid, oc, identity<T>());
  }
  template <typename T>
  T get(FLDID32 fieldid, FLDOCC32 oc, const T &default_value) {
    return get(fieldid, oc, default_value, identity<T>());
  }

  fml32buf &put(FLDID32 fieldid, FLDOCC32 oc, const std::string &value) {
    return update([&] {
      return CFchg32(*fbfr(), fieldid, oc, const_cast<char *>(value.data()),
                     value.size(), FLD_CARRAY);
    });
  }

  fml32buf &put(FLDID32 fieldid, FLDOCC32 oc, long value) {
    return update([&] {
      return CFchg32(*fbfr(), fieldid, oc, reinterpret_cast<char *>(&value),
                     sizeof(value), FLD_LONG);
    });
  }

  FBFR32 **fbfr() { return fbfr_; }

 private:
  fml32buf &update(std::function<int()> f) {
    while (true) {
      int rc = f();
      if (rc == -1) {
        if (Ferror32 == FNOSPACE) {
          *fbfr_ = reinterpret_cast<FBFR32 *>(tprealloc(
              reinterpret_cast<char *>(*fbfr_), Fsizeof32(*fbfr_) * 2));
        } else {
          throw fml32buf_error();
        }
      } else {
        break;
      }
    }
    return *this;
  }
  long get(FLDID32 fieldid, FLDOCC32 oc, identity<long>) {
    long ret;
    if (CFget32(*fbfr(), fieldid, oc, reinterpret_cast<char *>(&ret), nullptr,
                FLD_LONG) != -1) {
      return ret;
    }
    throw fml32buf_error();
  }
  long get(FLDID32 fieldid, FLDOCC32 oc, const long &default_value,
           identity<long>) {
    long ret;
    if (CFget32(*fbfr(), fieldid, oc, reinterpret_cast<char *>(&ret), nullptr,
                FLD_LONG) != -1) {
      return ret;
    }
    return default_value;
  }

  std::string get(FLDID32 fieldid, FLDOCC32 oc, identity<std::string>) {
    FLDLEN32 len;
    char *ret;
    if ((ret = CFfind32(*fbfr(), fieldid, oc, &len, FLD_CARRAY)) != nullptr) {
      return std::string(ret, len);
    }
    throw fml32buf_error();
  }

  std::string get(FLDID32 fieldid, FLDOCC32 oc,
                  const std::string &default_value, identity<std::string>) {
    FLDLEN32 len;
    char *ret;
    if ((ret = CFfind32(*fbfr(), fieldid, oc, &len, FLD_CARRAY)) != nullptr) {
      return std::string(ret, len);
    }
    return default_value;
  }

  FBFR32 **fbfr_;
  FBFR32 *owned_;
};

inline void tpreturn(int rval, long rcode, fml32buf &buf) {
  ::tpreturn(rval, rcode, reinterpret_cast<char *>(*buf.fbfr()), 0, 0);
}

inline void tpcall(const char *svc, fml32buf &buf, long flags) {
  long olen = 0;
  int n =
      ::tpcall(const_cast<char *>(svc), reinterpret_cast<char *>(*buf.fbfr()),
               0, reinterpret_cast<char **>(buf.fbfr()), &olen, flags);
  if (n == -1) {
    throw xatmi_error();
  }
}

inline void tpcall(const std::string &svc, fml32buf &buf, long flags) {
  tpcall(svc.c_str(), buf, flags);
}
}
