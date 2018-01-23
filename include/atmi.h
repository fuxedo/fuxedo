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
int tpunadvertise(char *svcname);

int tpinit(TPINIT *tpinfo);
int tpterm();
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

#ifdef __cplusplus
}
#endif
