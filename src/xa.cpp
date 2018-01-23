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

#include <xa.h>

int ax_reg(int rmid, XID *xid, long flags) { return TMER_TMERR; }
int ax_unreg(int rmid, long flags) { return TMER_TMERR; }

static int xa_open(char *xa_info, int rmid, long flags) { return XA_OK; }
static int xa_close(char *xa_info, int rmid, long flags) { return XA_OK; }
static int xa_start(XID *xid, int rmid, long flags) { return XA_OK; }
static int xa_commit(XID *xid, int rmid, long flags) { return XA_OK; }
static int xa_complete(int *handle, int *retval, int rmid, long flags) {
  return XA_OK;
}
static int xa_end(XID *xid, int rmid, long flags) { return XA_OK; }
static int xa_forget(XID *xid, int rmid, long flags) { return XA_OK; }
static int xa_prepare(XID *xid, int rmid, long flags) { return XA_RDONLY; }
static int xa_recover(XID *xids, long count, int rmid, long flags) {
  return XA_OK;
}
static int xa_rollback(XID *xid, int rmid, long flags) { return XA_OK; }

extern "C" {
struct xa_switch_t tmnull_switch {
  "null", TMNOFLAGS, 0, xa_open, xa_close, xa_start, xa_end, xa_rollback,
      xa_prepare, xa_commit, xa_recover, xa_forget, xa_complete
};
}
