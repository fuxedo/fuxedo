// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>

#include "fbfr32.h"
#include "misc.h"
#include "view32.h"

int Fvftos32(FBFR32 *fbfr, char *cstruct, char *viewname) {
  if (fbfr == nullptr) {
    FERROR(FALIGNERR, "fbfr is NULL");
    return -1;
  }
  if (cstruct == nullptr || viewname == nullptr) {
    FERROR(FEINVAL, "cstruct or viewname is NULL");
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        if (fux::view::to_struct(reinterpret_cast<Fbfr32 *>(fbfr), cstruct,
                                 *view) == -1) {
          return -1;
        }
        return 1;
      },
      -1);
}

int Fvstof32(FBFR32 *fbfr, char *cstruct, int mode, char *viewname) {
  if (fbfr == nullptr) {
    FERROR(FALIGNERR, "fbfr is NULL");
    return -1;
  }
  if (cstruct == nullptr || viewname == nullptr) {
    FERROR(FEINVAL, "cstruct or viewname is NULL");
    return -1;
  }
  if (mode != FUPDATE && mode != FJOIN && mode != FOJOIN && mode != FCONCAT) {
    FERROR(FEINVAL, "invalid mode %d", mode);
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        if (fux::view::from_struct(reinterpret_cast<Fbfr32 *>(fbfr), cstruct,
                                   mode, *view) == -1) {
          return -1;
        }
        return 1;
      },
      -1);
}

int Fvnull32(char *cstruct, char *cname, FLDOCC32 oc, char *viewname) {
  if (cstruct == nullptr || cname == nullptr || viewname == nullptr) {
    FERROR(FEINVAL, "NULL argument");
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        auto *m = fux::view::find_member(*view, cname);
        if (m == nullptr) {
          FERROR(FNOCNAME, "no member %s in view %s", cname, viewname);
          return -1;
        }
        if (oc < 0 || oc >= m->count) {
          FERROR(FNOCNAME, "occurrence %d out of range", oc);
          return 1;
        }
        return fux::view::is_null(*m, cstruct + m->off + oc * m->elsize) ? 1
                                                                         : 0;
      },
      -1);
}

int Fvsinit32(char *cstruct, char *viewname) {
  if (cstruct == nullptr || viewname == nullptr) {
    FERROR(FEINVAL, "NULL argument");
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        fux::view::sinit(cstruct, *view);
        return 1;
      },
      -1);
}

int Fvselinit32(char *cstruct, char *cname, char *viewname) {
  if (cstruct == nullptr || cname == nullptr || viewname == nullptr) {
    FERROR(FEINVAL, "NULL argument");
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        auto *m = const_cast<fux::view::member *>(
            fux::view::find_member(*view, cname));
        if (m == nullptr) {
          FERROR(FNOCNAME, "no member %s in view %s", cname, viewname);
          return -1;
        }
        fux::view::selinit(cstruct, *m);
        return 1;
      },
      -1);
}

int Fvopt32(char *cname, int option, char *viewname) {
  if (viewname == nullptr) {
    FERROR(FEINVAL, "viewname is NULL");
    return -1;
  }
  if (cname == nullptr) {
    FERROR(FNOTPRES, "cname is NULL");
    return -1;
  }
  if (option != F_OFF && option != F_FTOS && option != F_STOF &&
      option != F_BOTH) {
    FERROR(FEINVAL, "invalid option %d", option);
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        auto *m = const_cast<fux::view::member *>(
            fux::view::find_member(*view, cname));
        if (m == nullptr) {
          FERROR(FNOCNAME, "no member %s in view %s", cname, viewname);
          return -1;
        }
        m->flags &= ~(F_STOF | F_FTOS);
        m->flags |= option;
        return 1;
      },
      -1);
}

int Fvneeded32(char *viewname) {
  if (viewname == nullptr) {
    FERROR(FEINVAL, "viewname is NULL");
    return -1;
  }
  return fux::fml32::exception_boundary(
      [&]() -> int {
        auto view = fux::view::get(viewname);
        if (view == nullptr) {
          return -1;
        }
        return view->vsize;
      },
      -1);
}

void Fvrefresh32() { fux::view::refresh(); }
