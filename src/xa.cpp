// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>

int ax_reg(int, XID *, long) { return TMER_TMERR; }
int ax_unreg(int, long) { return TMER_TMERR; }

static int xa_open(char *, int, long) { return XA_OK; }
static int xa_close(char *, int, long) { return XA_OK; }
static int xa_start(XID *, int, long) { return XA_OK; }
static int xa_commit(XID *, int, long) { return XA_OK; }
static int xa_complete(int *, int *, int, long) { return XA_OK; }
static int xa_end(XID *, int, long) { return XA_OK; }
static int xa_forget(XID *, int, long) { return XA_OK; }
static int xa_prepare(XID *, int, long) { return XA_RDONLY; }
static int xa_recover(XID *, long, int, long) { return XA_OK; }
static int xa_rollback(XID *, int, long) { return XA_OK; }

extern "C" {
struct xa_switch_t tmnull_switch {
  "null", TMNOFLAGS, 0, xa_open, xa_close, xa_start, xa_end, xa_rollback,
      xa_prepare, xa_commit, xa_recover, xa_forget, xa_complete
};
}
