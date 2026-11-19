// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xatmi.h>
#include <algorithm>
#include <cstddef>
#include <cstring>

#include "compat.h"
#include "fbfr32.h"
#include "misc.h"

namespace fux::mem {

struct tptype {
  char type[8];
  char subtype[16];
  int default_size;
  void (*init)(void *mem, size_t size);
  void (*reinit)(void *mem, size_t size);
  void (*finit)(void *mem);
  size_t (*used)(void *mem);
};

size_t strused(void *ptr) { return strlen(reinterpret_cast<char *>(ptr)) + 1; }

static tptype _tptypes[] = {
    tptype{"TPINIT", "*", TPINITNEED(0), nullptr, nullptr, nullptr, nullptr},
    tptype{"CARRAY", "*", 0, nullptr, nullptr, nullptr, nullptr},
    tptype{"STRING", "*", 512, nullptr, nullptr, nullptr, strused},
    tptype{"FML32", "*", 512, fml32init, fml32reinit, fml32finit, fml32used},
    tptype{"XML", "*", 512, nullptr, nullptr, nullptr, strused}};

struct tpmem {
  long size;
  char **owner;
  char type[8];
  char subtype[16];
  char data[];
};

static tpmem *memptr(char *ptr) {
  return (tpmem *)(ptr - offsetof(struct tpmem, data));
}

static const tptype *typeptr(const char *type, const char *subtype) {
  const auto &tptype = std::find_if(
      std::cbegin(_tptypes), std::cend(_tptypes), [&](const auto &t) {
        return (strncmp(t.type, type, sizeof(t.type)) == 0 &&
                (subtype == nullptr || subtype[0] == '\0' ||
                 strncmp(t.subtype, subtype, sizeof(t.subtype)) == 0));
      });
  if (tptype == std::end(_tptypes)) {
    TPERROR(TPENOENT, "unknown type [%s] and subtype[%s]", type,
            subtype == nullptr ? "" : subtype);
    return nullptr;
  }
  return &(*tptype);
}

char *tpalloc(char *type, char *subtype, long size) {
  if (type == nullptr) {
    TPERROR(TPEINVAL, "type is nullptr");
    return nullptr;
  }

  const auto tptype = typeptr(type, subtype);
  if (tptype == nullptr) {
    return nullptr;
  }

  size = size >= tptype->default_size ? size : tptype->default_size;
  auto mem = (tpmem *)malloc(sizeof(tpmem) + size);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
  strncpy(mem->type, type, sizeof(mem->type));
#pragma GCC diagnostic pop
  if (subtype != nullptr) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(mem->subtype, subtype, sizeof(mem->subtype));
#pragma GCC diagnostic pop
  } else {
    mem->subtype[0] = '\0';
  }
  mem->size = size;
  mem->owner = nullptr;
  if (tptype->init != nullptr) {
    tptype->init(mem->data, size);
  }

  // the platform leaves tperrno untouched on tpalloc success (probed
  // on 12.2); only the tptypes/tpfree/tpexport/tpimport/tprealloc
  // family resets it
  return mem->data;
}

char *tprealloc(char *ptr, long size) {
  if (ptr == nullptr) {
    TPERROR(TPEINVAL, "ptr is nullptr");
    return nullptr;
  }

  auto mem = memptr(ptr);
  const auto tptype = typeptr(mem->type, mem->subtype);
  if (tptype == nullptr) {
    return nullptr;
  }

  size = (size >= tptype->default_size) ? size : tptype->default_size;
  mem = (tpmem *)realloc(mem, sizeof(tpmem) + size);
  if (tptype->reinit != nullptr) {
    tptype->reinit(mem->data, size);
  }

  if (mem->owner != nullptr && *(mem->owner) == ptr) {
    *(mem->owner) = mem->data;
  }
  fux::atmi::reset_tperrno();
  return mem->data;
}
void tpfree(char *ptr) {
  if (ptr != nullptr) {
    // Inside service routines do not free buffer passed into a service routine
    auto mem = memptr(ptr);
    const auto tptype = typeptr(mem->type, mem->subtype);
    if (tptype == nullptr) {
      return;
    }
    if (tptype->finit != nullptr) {
      tptype->finit(ptr);
    }
    if (mem->owner != nullptr && *(mem->owner) == ptr) {
      *(mem->owner) = nullptr;
    }
    free(mem);
  }
  fux::atmi::reset_tperrno();
}

long tptypes(char *ptr, char *type, char *subtype) {
  if (ptr == nullptr) {
    TPERROR(TPEINVAL, "ptr is nullptr");
    return -1;
  }

  auto mem = memptr(ptr);
  const auto tptype = typeptr(mem->type, mem->subtype);
  if (tptype == nullptr) {
    TPERROR(TPEINVAL, "Buffer not fielded");
    return -1;
  }
  if (type != nullptr) {
    std::copy_n(mem->type, sizeof(mem->type), type);
  }
  if (subtype != nullptr) {
    std::copy_n(mem->subtype, sizeof(mem->subtype), subtype);
  }

  fux::atmi::reset_tperrno();
  return 0;
}

int tpimport(char *istr, long ilen, char **obuf, long *olen, long flags) {
  if (istr == nullptr) {
    TPERROR(TPEINVAL, "istr is NULL");
    return -1;
  }
  if (obuf == nullptr) {
    TPERROR(TPEINVAL, "obuf is NULL");
    return -1;
  }
  if (!fux::compat::is221 && (flags & ~TPEX_STRING) != 0) {
    // 12.x rejects unknown flag bits; 22.x accepts and ignores them
    TPERROR(TPEINVAL, "Invalid flags");
    return -1;
  }

  if (ilen == 0) {
    flags |= TPEX_STRING;
  }

  long needed = sizeof(tpmem);
  if (flags & TPEX_STRING) {
    ilen = strlen(istr);
    if (ilen % 4) {
      // 12.x reports TPEPROTO, 22.x reports TPESYSTEM for malformed input
      if (fux::compat::is221) {
        TPERROR(TPESYSTEM, "Invalid base64 string");
      } else {
        TPERROR(TPEPROTO, "Invalid base64 string");
      }
      return -1;
    }
    needed += ilen / 4 * 3;
  } else {
    needed += ilen;
  }

  if (*obuf == nullptr) {
    // 22.x allocates the target buffer (the caller frees it with tpfree());
    // 12.x reports TPEINVAL
    if (!fux::compat::is221) {
      TPERROR(TPEINVAL, "*obuf is NULL");
      return -1;
    }
    constexpr auto prefix = offsetof(tpmem, type);
    char type[sizeof(tpmem::type) + 1] = {};
    char subtype[sizeof(tpmem::subtype) + 1] = {};
    char wire[prefix + 8] = {};
    if (flags & TPEX_STRING) {
      constexpr auto wire_b64 = base64chars(sizeof(wire));
      if (ilen < static_cast<long>(wire_b64)) {
        TPERROR(TPESYSTEM, "Import buffer too small for a buffer header");
        return -1;
      }
      base64decode(istr, wire_b64, wire, sizeof(wire));
    } else {
      if (ilen < static_cast<long>(prefix)) {
        TPERROR(TPESYSTEM, "Import buffer too small for a buffer header");
        return -1;
      }
      std::copy_n(istr, prefix, wire);
    }
    // the wire format starts at tpmem::type
    std::copy_n(wire, sizeof(tpmem::type), type);
    std::copy_n(wire + sizeof(tpmem::type), sizeof(tpmem::subtype), subtype);
    *obuf = tpalloc(type, subtype, needed - prefix);
    if (*obuf == nullptr) {
      return -1;
    }
  }

  auto omem = memptr(*obuf);
  if (needed > omem->size) {
    *obuf = tprealloc(*obuf, needed);
    omem = memptr(*obuf);
  }

  if (flags & TPEX_STRING) {
    (void)base64decode(istr, ilen,
                       reinterpret_cast<char *>(omem) + offsetof(tpmem, type),
                       ilen);
  } else {
    std::copy_n(istr, ilen,
                reinterpret_cast<char *>(omem) + offsetof(tpmem, type));
  }

  const auto tptype = typeptr(omem->type, omem->subtype);
  if (tptype == nullptr) {
    return -1;
  }
  if (tptype->reinit != nullptr) {
    tptype->reinit(omem->data, omem->size);
  }

  if (olen != nullptr) {
    *olen = ilen;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

int tpexport(char *ibuf, long ilen, char *ostr, long *olen, long flags) {
  if (ibuf == nullptr || ostr == nullptr || olen == nullptr) {
    TPERROR(TPEINVAL, "Invalid arguments");
    return -1;
  }
  if (!fux::compat::is221 && (flags & ~TPEX_STRING) != 0) {
    // 12.x rejects unknown flag bits (including 22.x-only TPEX_XDR64LONG);
    // 22.x accepts and ignores them
    TPERROR(TPEINVAL, "Invalid flags");
    return -1;
  }

  auto mem = memptr(ibuf);
  long used = fux::mem::bufsize(ibuf, ilen);
  if (used == -1) {
    return -1;
  }

  long needed;
  if (flags & TPEX_STRING) {
    needed = base64chars(used) + 1;
  } else {
    needed = used;
  }

  if (*olen < needed) {
    TPERROR(TPELIMIT, "Output buffer too small");
    *olen = needed;
    return -1;
  }

  if (flags & TPEX_STRING) {
    auto n = base64encode(reinterpret_cast<char *>(mem) + offsetof(tpmem, type),
                          used, ostr, *olen);
    ostr[n] = '\0';
  } else {
    std::copy_n(reinterpret_cast<char *>(mem) + offsetof(tpmem, type), used,
                ostr);
  }

  *olen = needed;
  fux::atmi::reset_tperrno();
  return 0;
}

void setowner(char *ptr, char **owner) { memptr(ptr)->owner = owner; }

long headersize() { return sizeof(tpmem) - offsetof(tpmem, type); }

long allocsize(char *ptr) {
  auto mem = memptr(ptr);
  return mem->size - offsetof(tpmem, type);
}

long bufsize(char *ptr, long used) {
  auto mem = memptr(ptr);
  const auto tptype = typeptr(mem->type, mem->subtype);
  if (tptype == nullptr) {
    return -1;
  }
  if (tptype->used != nullptr) {
    return tptype->used(mem->data) + sizeof(*mem) - offsetof(tpmem, type);
  } else if (used != -1) {
    return used + sizeof(*mem) - offsetof(tpmem, type);
  } else {
    return mem->size - offsetof(tpmem, type);
  }
}

}  // namespace fux::mem

char *tpalloc(char *type, char *subtype, long size) {
  return fux::atmi::exception_boundary(
      [&] { return fux::mem::tpalloc(type, subtype, size); }, nullptr);
}

char *tprealloc(char *ptr, long size) {
  return fux::atmi::exception_boundary(
      [&] { return fux::mem::tprealloc(ptr, size); }, nullptr);
}

void tpfree(char *ptr) {
  fux::atmi::exception_boundary([&] { fux::mem::tpfree(ptr); });
}

long tptypes(char *ptr, char *type, char *subtype) {
  return fux::atmi::exception_boundary(
      [&] { return fux::mem::tptypes(ptr, type, subtype); }, -1);
}

int tpimport(char *istr, long ilen, char **obuf, long *olen, long flags) {
  return fux::atmi::exception_boundary(
      [&] { return fux::mem::tpimport(istr, ilen, obuf, olen, flags); }, -1);
}
int tpexport(char *ibuf, long ilen, char *ostr, long *olen, long flags) {
  return fux::atmi::exception_boundary(
      [&] { return fux::mem::tpexport(ibuf, ilen, ostr, olen, flags); }, -1);
}
