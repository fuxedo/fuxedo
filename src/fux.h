#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

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

struct fml32ptr {
  fml32ptr() : pp(&p), p(nullptr) { reinit(); }
  fml32ptr(FBFR32 **f) : pp(f), p(nullptr) {}
  fml32ptr(TPSVCINFO *svcinfo)
      : pp(reinterpret_cast<FBFR32 **>(&svcinfo->data)), p(nullptr) {}
  void reinit() {
    if (*pp == nullptr) {
      *pp = reinterpret_cast<FBFR32 *>(
          tpalloc(const_cast<char *>("FML32"), const_cast<char *>("*"), 1024));
      if (*pp == nullptr) {
        throw std::bad_alloc();
      }
    } else {
      Finit32(*pp, Fsizeof32(*pp));
    }
  }
  fml32ptr(fml32ptr &&other) : fml32ptr() { swap(other); }
  fml32ptr &operator=(fml32ptr &&other) {
    swap(other);
    return *this;
  }
  ~fml32ptr() {
    if (p != nullptr) {
      tpfree(reinterpret_cast<char *>(p));
    }
  }

  fml32ptr(const fml32ptr &o) {
    reinit();
    *this = o;
  }
  fml32ptr &operator=(const fml32ptr &o) {
    mutate([&](FBFR32 *fbfr) { return Fcpy32(fbfr, *(o.pp)); });
    return *this;
  }

  FBFR32 *release() {
    FBFR32 *ret = p;
    p = nullptr;
    return ret;
  }

  void mutate(std::function<int(FBFR32 *)> f) {
    while (true) {
      int rc = f(*pp);
      if (rc == -1) {
        if (Ferror32 == FNOSPACE) {
          *pp = reinterpret_cast<FBFR32 *>(
              tprealloc(reinterpret_cast<char *>(*pp), Fsizeof32(*pp) * 2));
        } else {
          throw fml32buf_error();
        }
      } else {
        break;
      }
    }
  }

  FBFR32 *get() { return *pp; }

  FBFR32 *ptr() const { return *pp; }
  FBFR32 **ptrptr() const { return pp; }

 private:
  FBFR32 **pp;
  FBFR32 *p;
  void swap(fml32ptr &other) noexcept { std::swap(p, other.p); }
};

template <typename T>
struct identity {
  typedef T type;
};

class fml32buf {
 public:
  fml32buf() {}
  fml32buf(TPSVCINFO *svcinfo) : buf_(svcinfo) {}
  fml32buf(FBFR32 **owning_ptr) : buf_(owning_ptr) {}

  template <typename T>
  T get(FLDID32 fieldid, FLDOCC32 oc) {
    return get(fieldid, oc, identity<T>());
  }
  template <typename T>
  T get(FLDID32 fieldid, FLDOCC32 oc, const T &default_value) {
    return get(fieldid, oc, default_value, identity<T>());
  }
  template <int N>
  std::string get(FLDID32 fieldid, FLDOCC32 oc,
                  const char (&default_value)[N]) {
    return get(fieldid, oc, default_value, identity<std::string>());
  }

  fml32buf &put(FLDID32 fieldid, FLDOCC32 oc, const fml32buf &value) {
    buf_.mutate([&](FBFR32 *fbfr) {
      return Fchg32(fbfr, fieldid, oc, reinterpret_cast<char *>(value.ptr()),
                    0);
    });
    return *this;
  }

  fml32buf &put(FLDID32 fieldid, FLDOCC32 oc, const std::string &value) {
    buf_.mutate([&](FBFR32 *fbfr) {
      return CFchg32(fbfr, fieldid, oc, const_cast<char *>(value.data()),
                     value.size(), FLD_CARRAY);
    });
    return *this;
  }

  fml32buf &put(FLDID32 fieldid, FLDOCC32 oc, long value) {
    buf_.mutate([&](FBFR32 *fbfr) {
      return CFchg32(fbfr, fieldid, oc, reinterpret_cast<char *>(&value),
                     sizeof(value), FLD_LONG);
    });
    return *this;
  }

  FLDOCC32 count(FLDID32 fieldid) { return Foccur32(ptr(), fieldid); }

  FBFR32 *ptr() const { return buf_.ptr(); }
  FBFR32 **ptrptr() const { return buf_.ptrptr(); }

 private:
  fml32ptr buf_;

  int get(FLDID32 fieldid, FLDOCC32 oc, identity<int>) {
    return get(fieldid, oc, identity<long>());
  }

  long get(FLDID32 fieldid, FLDOCC32 oc, identity<long>) {
    long ret;
    if (CFget32(ptr(), fieldid, oc, reinterpret_cast<char *>(&ret), nullptr,
                FLD_LONG) != -1) {
      return ret;
    }
    throw fml32buf_error();
  }
  int get(FLDID32 fieldid, FLDOCC32 oc, const int &default_value,
          identity<int>) {
    return get(fieldid, oc, default_value, identity<long>());
  }
  long get(FLDID32 fieldid, FLDOCC32 oc, const long &default_value,
           identity<long>) {
    long ret;
    if (CFget32(ptr(), fieldid, oc, reinterpret_cast<char *>(&ret), nullptr,
                FLD_LONG) != -1) {
      return ret;
    }
    return default_value;
  }

  std::string get(FLDID32 fieldid, FLDOCC32 oc, identity<std::string>) {
    FLDLEN32 len;
    char *ret;
    if ((ret = CFfind32(ptr(), fieldid, oc, &len, FLD_CARRAY)) != nullptr) {
      return std::string(ret);
    }
    throw fml32buf_error();
  }

  std::string get(FLDID32 fieldid, FLDOCC32 oc,
                  const std::string &default_value, identity<std::string>) {
    FLDLEN32 len;
    char *ret;
    if ((ret = CFfind32(ptr(), fieldid, oc, &len, FLD_CARRAY)) != nullptr) {
      return std::string(ret);
    }
    return default_value;
  }
};

inline void tpreturn(int rval, long rcode, fml32buf &buf) {
  ::tpreturn(rval, rcode, reinterpret_cast<char *>(*buf.ptrptr()), 0, 0);
}

inline void tpcall(const char *svc, fml32buf &buf, long flags) {
  long olen = 0;
  int n =
      ::tpcall(const_cast<char *>(svc), reinterpret_cast<char *>(*buf.ptrptr()),
               0, reinterpret_cast<char **>(buf.ptrptr()), &olen, flags);
  if (n == -1) {
    throw xatmi_error();
  }
}

inline void tpcall(const std::string &svc, fml32buf &buf, long flags) {
  tpcall(svc.c_str(), buf, flags);
}
}  // namespace fux
