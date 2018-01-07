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
#include <cstddef>
#include <cstring>

#include <vector>
#include "misc.h"

#include <iostream>

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

void fml32init(void *, size_t);
void fml32reinit(void *, size_t);
void fml32finit(void *);
size_t fml32used(void *);

std::vector<tptype> _tptypes = {
    tptype{"CARRAY", "*", 0, nullptr, nullptr, nullptr, nullptr},
    tptype{"STRING", "*", 512, nullptr, nullptr, nullptr, strused},
    tptype{"FML32", "*", 512, fml32init, fml32reinit, fml32finit, fml32used}};

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

static tptype *typeptr(const char *type, const char *subtype) {
  const auto &tptype =
      std::find_if(_tptypes.begin(), _tptypes.end(), [&](const auto &t) {
        return (strncmp(t.type, type, sizeof(t.type)) == 0 &&
                (subtype == nullptr || subtype[0] == '\0' ||
                 strncmp(t.subtype, subtype, sizeof(t.subtype)) == 0));
      });
  if (tptype == _tptypes.end()) {
    TPERROR(TPENOENT, "unknown type [%s] and subtype[%s]", type,
            subtype == nullptr ? "" : subtype);
    return nullptr;
  }
  return &(*tptype);
}

char *tpalloc(const char *type, const char *subtype, long size) try {
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
  strncpy(mem->type, type, sizeof(mem->type));
  if (subtype != nullptr) {
    strncpy(mem->subtype, subtype, sizeof(mem->subtype));
  } else {
    mem->subtype[0] = '\0';
  }
  mem->size = size;
  mem->owner = nullptr;
  if (tptype->init != nullptr) {
    tptype->init(mem->data, size);
  }

  return mem->data;
} catch (...) {
  return nullptr;
}

char *tprealloc(char *ptr, long size) try {
  if (ptr == nullptr) {
    TPERROR(TPEINVAL, "ptr is nullptr");
    return nullptr;
  }

  auto mem = memptr(ptr);
  const auto tptype = typeptr(mem->type, mem->subtype);
  if (tptype == nullptr) {
    return nullptr;
  }

  size = size >= tptype->default_size ? size : tptype->default_size;
  mem = (tpmem *)realloc(mem, sizeof(tpmem) + size);
  if (tptype->reinit != nullptr) {
    tptype->reinit(mem, size);
  }

  if (mem->owner != nullptr && *(mem->owner) == ptr) {
    *(mem->owner) = mem->data;
  }
  return mem->data;
} catch (...) {
  return nullptr;
}

void tpfree(char *ptr) try {
  if (ptr != nullptr) {
    // Inside service routines do not free buffer past into a service routine
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
} catch (...) {
}

long tptypes(char *ptr, char *type, char *subtype) try {
  if (ptr == nullptr) {
    TPERROR(TPEINVAL, "ptr is nullptr");
    return -1;
  }

  auto mem = memptr(ptr);
  if (type != nullptr) {
    std::copy_n(mem->type, sizeof(mem->type), type);
  }
  if (subtype != nullptr) {
    std::copy_n(mem->subtype, sizeof(mem->subtype), subtype);
  }

  return 0;
} catch (...) {
  return -1;
}

int tpimport(char *istr, long ilen, char **obuf, long *olen, long flags) try {
  if (istr == nullptr) {
    TPERROR(TPEINVAL, "istr is NULL");
    return -1;
  }
  if (obuf == nullptr || *obuf == nullptr) {
    TPERROR(TPEINVAL, "obuf is NULL");
    return -1;
  }

  if (ilen == 0) {
    flags |= TPEX_STRING;
  }

  long needed = sizeof(tpmem);
  if (flags & TPEX_STRING) {
    ilen = strlen(istr);
    if (ilen % 4) {
      TPERROR(TPEPROTO, "Invalid base64 string");
      return -1;
    }
    needed += ilen / 4 * 3;
  } else {
    needed += ilen;
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
  return 0;
} catch (...) {
  TPERROR(TPEPROTO, "Invalid base64 string");
  return -1;
}

int tpexport(char *ibuf, long ilen, char *ostr, long *olen, long flags) try {
  if (ibuf == nullptr || ostr == nullptr || olen == nullptr) {
    TPERROR(TPEINVAL, "Invalid arguments");
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
  return 0;
} catch (...) {
  return -1;
}

namespace fux {
namespace mem {
void setowner(char *ptr, char **owner) { memptr(ptr)->owner = owner; }

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
}
}
