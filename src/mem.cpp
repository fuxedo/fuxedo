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

struct tptype {
  char type[8];
  char subtype[16];
  int default_size;
  void (*init)(void *mem, size_t size);
  void (*reinit)(void *mem, size_t size);
  void (*finit)(void *mem);
};

void fml32init(void *, size_t);
void fml32reinit(void *, size_t);
void fml32finit(void *);

std::vector<tptype> _tptypes = {
    tptype{"CARRAY", "*", 0, nullptr, nullptr, nullptr},
    tptype{"STRING", "*", 512, nullptr, nullptr, nullptr},
    tptype{"FML32", "*", 512, fml32init, fml32reinit, fml32finit}};

struct tpmem {
  char type[8];
  char subtype[16];
  long size;
  char data[];
};

static tpmem *memptr(char *ptr) {
  return (tpmem *)(ptr - offsetof(struct tpmem, data));
}

static tptype *typeptr(const char *type, const char *subtype) {
  const auto &tptype =
      std::find_if(_tptypes.begin(), _tptypes.end(), [&](const auto &t) {
        return (strncmp(t.type, type, sizeof(t.type)) == 0 &&
                (subtype == nullptr ||
                 strncmp(t.subtype, subtype, sizeof(t.subtype)) == 0));
      });
  if (tptype == _tptypes.end()) {
    TPERROR(TPENOENT, "unknown type [%s] and subtype[%s]", type,
            subtype == nullptr ? "" : subtype);
    return nullptr;
  }
  return &(*tptype);
}

char *tpalloc(const char *type, const char *subtype, long size) {
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
  if (tptype->init != nullptr) {
    tptype->init(mem->data, size);
  }

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

  size = size >= tptype->default_size ? size : tptype->default_size;
  mem = (tpmem *)realloc(mem, sizeof(tpmem) + size);
  if (tptype->reinit != nullptr) {
    tptype->reinit(ptr, size);
  }
  return mem->data;
}

void tpfree(char *ptr) {
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
    free(mem);
  }
}

long tptypes(char *ptr, char *type, char *subtype) {
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
}

int tpimport(char *istr, long ilen, char **obuf, long *olen, long flags);
int tpexport(char *ibuf, long ilen, char *ostr, long *olen, long flags);
