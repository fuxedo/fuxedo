// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml.h>
#include <string.h>

#include "misc.h"
#include "view32.h"

namespace {
struct fbfr16_header {
  unsigned short buflen;
  unsigned short used;
  int count;
};
}  // namespace

void fml16init(void *mem, size_t size) {
  auto *h = static_cast<fbfr16_header *>(mem);
  h->buflen = static_cast<unsigned short>(
      size > sizeof(fbfr16_header) ? size - sizeof(fbfr16_header) : 0);
  h->used = 0;
  h->count = 0;
}

void fml16reinit(void *mem, size_t size) {
  // tpimport calls reinit AFTER copying the wire bytes: the imported
  // record area is already the complete state, nothing to rebuild
  (void)mem;
  (void)size;
}

size_t fml16used(void *mem) {
  auto *h = static_cast<fbfr16_header *>(mem);
  return sizeof(fbfr16_header) + h->used;
}

size_t view16used(void *mem) {
  // the tpmem header sits right before the data area
  struct tpmem_like {
    long size;
    char **owner;
    char type[16];
    char subtype[16];
    char data[0];
  };
  auto *m = reinterpret_cast<tpmem_like *>(
      static_cast<char *>(mem) - offsetof(tpmem_like, data));
  auto used = fux::view::view_used(m->subtype, m->size);
  return used == 0 ? static_cast<size_t>(m->size) : used;
}

size_t view32used(void *mem) {
  // the tpmem header sits right before the data area
  struct tpmem_like {
    long size;
    char **owner;
    char type[16];
    char subtype[16];
    char data[0];
  };
  auto *m = reinterpret_cast<tpmem_like *>(
      static_cast<char *>(mem) - offsetof(tpmem_like, data));
  auto used = fux::view::view32_used(m->subtype, m->size);
  return used == 0 ? static_cast<size_t>(m->size) : used;
}
