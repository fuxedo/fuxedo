// Oracle XA test server (Phase 1 of the multi-RM matrix), OCI edition.
//
// INSERT      plain service; AUTOTRAN=Y makes the platform begin a
//             transaction for it when the caller has none
// INSERTFAIL  returns TPFAIL; with AUTOTRAN=Y the outcome is an abort
// INSERTOWN   begins its own transaction, never commits; the platform
//             commits it on TPSUCCESS at tpreturn (pin)
//
// fuxedo starts the XA branch at dispatch; this server joins the same
// branch from its OCI session through DBMS_XA (the documented
// server-side XA interface), so statement work rides the branch that
// the TMS prepares/commits/rolls back. tpsuspend exposes the XID; the
// dispatch thread's own association is left alone.

#include <atmi.h>
#include <fml32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <userlog.h>

#include <oci.h>

#include "orarm.h"

static OCIEnv *env;
static OCIError *err;
static OCISvcCtx *svc;
static OCIStmt *stmt;
static boolean attached = FALSE;

static void bail(const char *what, sword status) {
  text msg[512];
  sb4 code;
  if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
    return;
  }
  OCIErrorGet(err, 1, NULL, &code, msg, sizeof(msg), OCI_HTYPE_ERROR);
  userlog("ORARM: %s failed: %.*s", what, 512, msg);
  tperrno = TPESYSTEM;
  tpreturn(TPFAIL, 0, NULL, 0, 0);
}

static int open_session() {
  if (attached) {
    return 0;
  }
  bail("OCILogon",
       OCILogon(env, err, &svc, (text *)getenv("ORARM_USER"),
                strlen(getenv("ORARM_USER")), (text *)getenv("ORARM_PASS"),
                strlen(getenv("ORARM_PASS")), (text *)getenv("ORARM_DB"),
                strlen(getenv("ORARM_DB"))));
  bail("OCIHandleAlloc(stmt)",
       OCIHandleAlloc(env, (void **)&stmt, OCI_HTYPE_STMT, 0, NULL));
  attached = TRUE;
  return 0;
}

static void exec_sql(const char *sql) {
  bail("OCIStmtPrepare",
       OCIStmtPrepare(stmt, err, (text *)sql, strlen(sql), OCI_NTV_SYNTAX,
                      OCI_DEFAULT));
  bail("OCIStmtExecute",
       OCIStmtExecute(svc, stmt, err, 1, 0, NULL, NULL, OCI_DEFAULT));
}

// the platform's text form: "<formatid>.<gtrid-hex>.<bqual-hex>"
static void xid_text(const XID *xid, char *out, size_t n) {
  size_t used = 0;
  used += snprintf(out + used, n - used, "%ld.", xid->formatID);
  for (int i = 0; i < xid->gtrid_length; i++) {
    used += snprintf(out + used, n - used, "%02x",
                     (unsigned char)xid->data[i]);
  }
  used += snprintf(out + used, n - used, ".");
  for (int i = 0; i < xid->bqual_length; i++) {
    used += snprintf(out + used, n - used, "%02x",
                     (unsigned char)xid->data[xid->gtrid_length + i]);
  }
}

static int do_insert(TPSVCINFO *msg) {
  if (open_session() == -1) {
    return -1;
  }

  long id = 0;
  if (Fget32((FBFR32 *)msg->data, FLD_ID, 0, (char *)&id, 0) == -1) {
    tperrno = TPEINVAL;
    return -1;
  }
  char tag[64] = {};
  FLDLEN32 len = sizeof(tag);
  if (Fget32((FBFR32 *)msg->data, FLD_TAG, 0, tag, &len) == -1) {
    tperrno = TPEINVAL;
    return -1;
  }
  if (strcmp(tag, "slow") == 0) {
    sleep(2);
  }

  (void)msg;
  char isql[256];
  snprintf(isql, sizeof(isql),
           "insert into fuxedo_xa_test (id, tag) values (%ld, '%s')", id,
           tag);
  exec_sql(isql);
  return 0;
}

void INSERT(TPSVCINFO *msg) {
  if (do_insert(msg) == -1) {
    tpreturn(TPFAIL, 0, NULL, 0, 0);
  }
  tpreturn(TPSUCCESS, 0, msg->data, 0, 0);
}

void INSERTFAIL(TPSVCINFO *msg) {
  if (do_insert(msg) == -1) {
    tpreturn(TPFAIL, 0, NULL, 0, 0);
  }
  tpreturn(TPFAIL, 0, msg->data, 0, 0);
}

void INSERTOWN(TPSVCINFO *msg) {
  if (tpbegin(30, 0) == -1) {
    tpreturn(TPFAIL, 0, NULL, 0, 0);
  }
  if (do_insert(msg) == -1) {
    tpreturn(TPFAIL, 0, NULL, 0, 0);
  }
  // no commit: the platform decides at tpreturn (pinned: TPSUCCESS
  // commits a server-begun transaction)
  tpreturn(TPSUCCESS, 0, msg->data, 0, 0);
}

int tpsvrinit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (OCIEnvCreate(&env, OCI_DEFAULT, NULL, NULL, NULL, NULL, 0, NULL) !=
      OCI_SUCCESS) {
    userlog("ORARM: OCIEnvCreate failed");
    return -1;
  }
  if (OCIHandleAlloc(env, (void **)&err, OCI_HTYPE_ERROR, 0, NULL) !=
      OCI_SUCCESS) {
    return -1;
  }
  if (tpadvertise("INSERT", INSERT) == -1 ||
      tpadvertise("INSERTFAIL", INSERTFAIL) == -1 ||
      tpadvertise("INSERTOWN", INSERTOWN) == -1) {
    return -1;
  }
  // open the resource manager for this group; failures must fail the
  // boot (xa_open errors surface here)
  if (tpopen() == -1) {
    userlog("ORARM: tpopen failed: %s", tpstrerror(tperrno));
    return -1;
  }
  return 0;
}

void tpsvrdone(void) {
  if (attached) {
    // never leave uncommitted work behind on our private session
    OCIStmtPrepare(stmt, err, (text *)"rollback", strlen("rollback"),
                   OCI_NTV_SYNTAX, OCI_DEFAULT);
    OCIStmtExecute(svc, stmt, err, 1, 0, NULL, NULL, OCI_DEFAULT);
    OCILogoff(svc, err);
    attached = FALSE;
  }
  tpclose();
}
