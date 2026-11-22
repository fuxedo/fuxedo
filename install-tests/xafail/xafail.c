// Scripted XA resource manager for failure injection (pinned
// xabackend analog). Plain XA, portable across Tuxedo-compatible
// stacks: buildtms -r XAFAIL links this switch, the knob is the
// XAFAIL_AT env bitmask: 1 = xa_start fails, 2 = xa_prepare fails,
// 4 = xa_commit fails. Default behavior matches the null switch.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xa.h>

// the knob file path arrives in OPENINFO after "XAFAIL:"; re-read on
// every call so the test can flip it between phases without rebooting
static char knobfile[512];
static unsigned long knob() {
  if (knobfile[0] == '\0') {
    return 0;
  }
  FILE *f = fopen(knobfile, "r");
  if (f == NULL) {
    return 0;
  }
  unsigned long v = 0;
  if (fscanf(f, "%lu", &v) != 1) {
    v = 0;
  }
  fclose(f);
  return v;
}

static int xa_open(char *info, int, long) {
  if (info != NULL) {
    snprintf(knobfile, sizeof(knobfile), "%s", info);
  }
  return XA_OK;
}
static int xa_close(char *, int, long) { return XA_OK; }
static int xa_start(XID *, int, long) {
  return (knob() & 1) ? XAER_RMERR : XA_OK;
}
static int xa_end(XID *, int, long) { return XA_OK; }
static int xa_rollback(XID *, int, long) { return XA_OK; }
static int xa_prepare(XID *, int, long) {
  return (knob() & 2) ? XAER_RMERR : XA_RDONLY;
}
static int xa_commit(XID *, int, long) {
  return (knob() & 4) ? XAER_RMERR : XA_OK;
}
static int xa_recover(XID *, long, int, long) { return 0; }
static int xa_forget(XID *, int, long) { return XA_OK; }
static int xa_complete(int *, int *, int, long) { return XA_OK; }

struct xa_switch_t xa_fail_switch = {
    "XAFAIL", TMNOFLAGS, 0, xa_open,     xa_close,   xa_start,   xa_end,
    xa_rollback,          xa_prepare,     xa_commit,  xa_recover, xa_forget,
    xa_complete};
