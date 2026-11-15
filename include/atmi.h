#pragma once
#include "atmidefs.h"
#include "tx.h"

#ifdef __cplusplus
extern "C" {
#endif

int *_tls_tperrno();
#define tperrno (*_tls_tperrno())
long *_tls_tpurcode();
#define tpurcode (*_tls_tpurcode())
char *tpstrerror(int err);

char *tpalloc(char *type, char *subtype, long size);
char *tprealloc(char *ptr, long size);
void tpfree(char *ptr);
long tptypes(char *ptr, char *type, char *subtype);

#define TPEX_STRING 1
int tpimport(char *istr, long ilen, char **obuf, long *olen, long flags);
int tpexport(char *ibuf, long ilen, char *ostr, long *olen, long flags);

int tppost(char *eventname, char *data, long len, long flags);
int tpgetctxt(TPCONTEXT_T *context, long flags);
int tpsetctxt(TPCONTEXT_T context, long flags);

int tpdequeue(char *qspace, char *qname, TPQCTL *ctl, char **data, long *len,
              long flags);

int tprminit(char *, void *);
int tpsvrinit(int argc, char **argv);
void tpsvrdone();
int tpsvrthrinit(int argc, char **argv);
void tpsvrthrdone();

struct tmdsptchtbl_t {
  const char *svcname;
  const char *funcname;
  void (*svcfunc)(TPSVCINFO *);
  int index;
  char flag;
  void *dummy;
};

struct xa_switch_t;

struct tmsvrargs_t {
  struct xa_switch_t *xa_switch;
  struct tmdsptchtbl_t *tmdsptchtbl;
  int flags;
  int (*svrinit)(int, char **);
  void (*svrdone)();
  int (*mainloop)(int);
  void *reserved1;
  void *reserved2;
  void *dummy0;
  void *dummy1;
  int (*rminit)(char *, void *);
  int (*svrthrinit)(int, char **);
  void (*svrthrdone)();
  void *dummy2;
};

int _tmstartserver(int argc, char **argv, struct tmsvrargs_t *tmsvrargs);

int tpadvertise(char *svcname, void (*func)(TPSVCINFO *));
int tpadvertisex(char *svcname, void (*func)(TPSVCINFO *), long flags);
int tpunadvertise(char *svcname);

int tpinit(TPINIT *tpinfo);
int tpterm();
int tpappthrinit(TPINIT *tpinifo);
int tpappthrterm();
int tpcall(char *svc, char *idata, long ilen, char **odata, long *olen,
           long flags);
int tpacall(char *svc, char *data, long len, long flags);
int tpcancel(int cd);

void tpreturn(int rval, long rcode, char *data, long len, long flags);

int tpbegin(unsigned long timeout, long flags);
int tpabort(long flags);
int tpcommit(long flags);
int tpgetlev();
int tpsuspend(TPTRANID *tranid, long flags);
int tpresume(TPTRANID *tranid, long flags);
int tpopen(void);
int tpclose(void);
int tpenqueue(char *qspace, char *qname, TPQCTL *ctl, char *data, long len,
              long flags);
void tpforward(char *svc, char *data, long len, long flags);
int tpgetrply(int *cd, char **data, long *len, long flags);

#define TPBLK_NEXT 0x01
#define TPBLK_ALL 0x02
#define TPBLK_SECOND 0x00
#define TPBLK_MILLISECOND 0x01000000
int tpsblktime(int blktime, long flags);
int tpgblktime(long flags);

#define TPSINGLETON 0x1   /* singleton service */
#define TPSECONDARYRQ 0x2 /* service on the secondary queue */
#define TPENOSINGLETON 26
#define TPENOSECONDARYRQ 27

#define AUXIDX_FLAGS 0
#define AUXIDX_MSGCNT 1
#define AUXIDX_QSPACEUSAGE 2 /* Index for store queue space usage */
#define AUXIDX_TGTBLOCK 3
#define MAXECIDLEN 64
#define MBAUTOCONVERSION_OFF 0 /*turn off autoconversion for buffers*/
#define MBAUTOCONVERSION_ON 1 /*turn on autoconversion for buffers*/
#define RM_ENC 0x00000001 /*delete encoding name on typed buffer*/
#define TDC_ERR_CACHE_NOTEXIST 100 /*The error code indicates the requested Tuxedo cache does not exist.*/
#define TDC_ERR_CACHE_UNAVAIL 101 /*The error code indicates the requested Tuxedo cache is unavailable.*/
#define TDC_ERR_KEY_NOTEXIST 200 /*The error code indicates the requested entry does not exist according to the specified key.*/
#define TMQSNAMELEN 15
#define TMSRVRFLAG_COBOL 0x00000001
#define TPAPPAUTH 2 /* system and application authentication */
#define TPBLK_MICROSECOND 0x02000000
#define TPBLK__MASK 0xff000000 /* mask to flag */
#define TPCONVCLTID 0x00000001 /* Convert CLIENTID */
#define TPCONVMAXSTR 256 /* Maximum string size */
#define TPCONVTRANID 0x00000002 /* Convert TRANID */
#define TPCONVXID 0x00000004 /* Convert XID */
#define TPED_CLIENTDISCONNECTED 6
#define TPED_DECRYPTION_FAILURE 11
#define TPED_DECUTCM_FAILURE 15
#define TPED_DOMAINUNREACHABLE 5
#define TPED_GROUP_FORBIDDEN 16
#define TPED_INVALIDCONTEXT 12
#define TPED_INVALID_CERTIFICATE 9
#define TPED_INVALID_SIGNATURE 10
#define TPED_INVALID_XA_TRANSACTION 13
#define TPED_MAXVAL 20 /* maximum error message */
#define TPED_MBCONV 14
#define TPED_MINVAL 0 /* minimum error message */
#define TPED_NOCLIENT 4
#define TPED_NOUNSOLHANDLER 3
#define TPED_OTS_INTERNAL 8
#define TPED_PERM 7
#define TPED_RDMA_INVALIDQUEUE 18
#define TPED_RDMA_MSGQDAEMON 17
#define TPED_RDMA_NOMEMORY 19
#define TPED_SVCTIMEOUT 1
#define TPED_TERM 2
#define TPEVRESERVED1 0x00000010 /* Reserved for future usage */
#define TPEVRESERVED2 0x00000020 /* Reserved for future usage */
#define TPEVRESERVED3 0x00000040 /* Reserved for future usage */
#define TPINVALIDCONTEXT -1
#define TPKEY_AUTOENCRYPT 0x00000200
#define TPKEY_AUTOSIGN 0x00000100
#define TPKEY_DECRYPT 0x00000020
#define TPKEY_ENCRYPT 0x00000040
#define TPKEY_REMOVE 0x00000400
#define TPKEY_REMOVEALL 0x00000800
#define TPKEY_SIGNATURE 0x00000010
#define TPKEY_VERIFICATION 0x00000080
#define TPKEY_VERIFY 0x00001000
#define TPNOAUTH 0 /* no authentication */
#define TPNULLCONTEXT -2
#define TPQCICSCANCEL 0x40000000 /* CICS ART CANCEL DELAY operation */
#define TPQENQGETUSAGE 0x80000000 /* Get space usage for tpenque */
#define TPSEAL_EXPIRED_CERT 0x00000004
#define TPSEAL_OK 0x00000001
#define TPSEAL_PENDING 0x00000002
#define TPSEAL_REVOKED_CERT 0x00000008
#define TPSEAL_TAMPERED_CERT 0x00000010
#define TPSEAL_UNKNOWN 0x00000020
#define TPSIGN_EXPIRED 0x00000100
#define TPSIGN_EXPIRED_CERT 0x00000200
#define TPSIGN_OK 0x00000040
#define TPSIGN_PENDING 0x00000080
#define TPSIGN_POSTDATED 0x00000400
#define TPSIGN_REVOKED_CERT 0x00000800
#define TPSIGN_TAMPERED_CERT 0x00001000
#define TPSIGN_TAMPERED_MESSAGE 0x00002000
#define TPSIGN_UNKNOWN 0x00004000
#define TPSINGLECONTEXT 0
#define TPSYSAUTH 1 /* system authentication */
#define TPTOSTRING 0x40000000 /* Convert structure to string */
#define TSQ 0x20000000 /* TSQ operation */
#define X_COMMON "X_COMMON"
#define X_C_TYPE "X_C_TYPE"
#define X_OCTET "X_OCTET"

#ifdef __cplusplus
}
#endif
