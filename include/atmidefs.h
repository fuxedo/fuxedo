#pragma once
// Definitions below come from Oracle Tuxedo documentation with minor fixes to
// make it compile and clang-format
// https://docs.oracle.com/cd/E72452_01/tuxedo/docs1222/rf3c/rf3c.html
#define XATMI_SERVICE_NAME_LENGTH 32

/* The following definitions must be included in atmi.h
 */

/* Flags to service routines */

#define TPNOBLOCK 0x00000001     /* non-blocking send/rcv */
#define TPSIGRSTRT 0x00000002    /* restart rcv on interrupt */
#define TPNOREPLY 0x00000004     /* no reply expected */
#define TPNOTRAN 0x00000008      /* not sent in transaction mode */
#define TPTRAN 0x00000010        /* sent in transaction mode */
#define TPNOTIME 0x00000020      /* no timeout */
#define TPABSOLUTE 0x00000040    /* absolute value on tmsetprio */
#define TPGETANY 0x00000080      /* get any valid reply */
#define TPNOCHANGE 0x00000100    /* force incoming buffer to match */
#define RESERVED_BIT1 0x00000200 /* reserved for future use */
#define TPCONV 0x00000400        /* conversational service */
#define TPSENDONLY 0x00000800    /* send-only mode */
#define TPRECVONLY 0x00001000    /* recv-only mode */
#define TPACK 0x00002000         /* */

/* Flags to tpreturn - also defined in xa.h */
#define TPFAIL 0x20000000    /* service FAILURE for tpreturn */
#define TPEXIT 0x08000000    /* service FAILURE with server exit */
#define TPSUCCESS 0x04000000 /* service SUCCESS for tpreturn */

/* Flags to tpscmt - Valid TP_COMMIT_CONTROL
 * characteristic values
 */
#define TP_CMT_LOGGED         \
  0x01 /* return after commit \
        * decision is logged */
#define TP_CMT_COMPLETE           \
  0x02 /* return after commit has \
        * completed */

/* client identifier structure */
struct clientid_t {
  long clientdata[4]; /* reserved for internal use */
};
typedef struct clientid_t CLIENTID;
/* context identifier structure */
typedef long TPCONTEXT_T;
/* interface to service routines */
struct tpsvcinfo {
  char name[XATMI_SERVICE_NAME_LENGTH];
  long flags;     /* describes service attributes */
  char *data;     /* pointer to data */
  long len;       /* request data length */
  int cd;         /* connection descriptor
                   * if (flags TPCONV) true */
  long appkey;    /* application authentication client
                   * key */
  CLIENTID cltid; /* client identifier for originating
                   * client */
};

typedef struct tpsvcinfo TPSVCINFO;

/* tpinit(3c) interface structure */
#define MAXTIDENT 30

struct tpinfo_t {
  char usrname[MAXTIDENT + 2]; /* client user name */
  char cltname[MAXTIDENT + 2]; /* app client name */
  char passwd[MAXTIDENT + 2];  /* application password */
  long flags;                  /* initialization flags */
  long datalen;                /* length of app specific data */
  long data;                   /* placeholder for app data */
};
typedef struct tpinfo_t TPINIT;

/* The transactionID structure passed to tpsuspend(3c) and tpresume(3c) */
struct tp_tranid_t {
  long info[6]; /* Internally defined */
};

typedef struct tp_tranid_t TPTRANID;

/* Flags for TPINIT */
#define TPU_MASK                         \
  0x00000007 /* unsolicited notification \
              * mask */
#define TPU_SIG              \
  0x00000001 /* signal based \
              * notification */
#define TPU_DIP              \
  0x00000002 /* dip-in based \
              * notification */
#define TPU_IGN                                     \
  0x00000004                  /* ignore unsolicited \
                               * messages */
#define TPU_THREAD 0x00000040 /* THREAD notification */
#define TPSA_FASTPATH            \
  0x00000008 /* System access == \
              * fastpath */
#define TPSA_PROTECTED           \
  0x00000010 /* System access == \
              * protected */
#define TPMULTICONTEXTS                   \
  0x00000020 /* multiple context associa- \
              * tions per process */
/* /Q tpqctl_t data structure */
#define TMQNAMELEN 127
#define TMMSGIDLEN 32
#define TMCORRIDLEN 32

struct tpqctl_t {                  /* control parameters to queue primitives */
  long flags;                      /* indicates which values are set */
  long deq_time;                   /* absolute/relative time for dequeuing */
  long priority;                   /* enqueue priority */
  long diagnostic;                 /* indicates reason for failure */
  char msgid[TMMSGIDLEN];          /* ID of message before which to queue */
  char corrid[TMCORRIDLEN];        /* correlation ID used to identify message */
  char replyqueue[TMQNAMELEN + 1]; /* queue name for reply message */
  char failurequeue[TMQNAMELEN + 1]; /* queue name for failure message */
  CLIENTID cltid;                    /* client identifier for */
  /* originating client */
  long urcode;       /* application user-return code */
  long appkey;       /* application authentication client key */
  long delivery_qos; /* delivery quality of service */
  long reply_qos;    /* reply message quality of service */
  long exp_time;     /* expiration time */
};
typedef struct tpqctl_t TPQCTL;

/* /Q structure elements that are valid - set in flags */
#ifndef TPNOFLAGS
#define TPNOFLAGS 0x00000 /* no flags set -- no get */
#endif
#define TPQCORRID 0x00001         /* set/get correlation ID */
#define TPQFAILUREQ 0x00002       /* set/get failure queue */
#define TPQBEFOREMSGID 0x00004    /* enqueue before message ID */
#define TPQGETBYMSGIDOLD 0x00008  /* deprecated */
#define TPQMSGID 0x00010          /* get msgid of enq/deq message */
#define TPQPRIORITY 0x00020       /* set/get message priority */
#define TPQTOP 0x00040            /* enqueue at queue top */
#define TPQWAIT 0x00080           /* wait for dequeuing */
#define TPQREPLYQ 0x00100         /* set/get reply queue */
#define TPQTIME_ABS 0x00200       /* set absolute time */
#define TPQTIME_REL 0x00400       /* set relative time */
#define TPQGETBYCORRIDOLD 0x00800 /* deprecated */
#define TPQPEEK 0x01000           /* non-destructive dequeue */
#define TPQDELIVERYQOS 0x02000    /* delivery quality of service */
#define TPQREPLYQOS 0x04000       /* reply msg quality of service*/
#define TPQEXPTIME_ABS 0x08000    /* absolute expiration time */
#define TPQEXPTIME_REL 0x10000    /* relative expiration time */
#define TPQEXPTIME_NONE 0x20000   /* never expire */
#define TPQGETBYMSGID 0x40008     /* dequeue by msgid */
#define TPQGETBYCORRID 0x80800    /* dequeue by corrid */

/* Valid flags for the quality of service fields in the TPQCTL structure */
#define TPQQOSDEFAULTPERSIST 0x00001 /* queue's default persistence */
/* policy */
#define TPQQOSPERSISTENT 0x00002    /* disk message */
#define TPQQOSNONPERSISTENT 0x00004 /* memory message */

/* error return codes */
extern int tperrno;
extern long tpurcode;

/* tperrno values - error codes */
/* The reference pages explain the context in which the following
 * error codes can return.
 */

#define TPMINVAL 0 /* minimum error message */
#define TPEABORT 1
#define TPEBADDESC 2
#define TPEBLOCK 3
#define TPEINVAL 4
#define TPELIMIT 5
#define TPENOENT 6
#define TPEOS 7
#define TPEPERM 8
#define TPEPROTO 9
#define TPESVCERR 10
#define TPESVCFAIL 11
#define TPESYSTEM 12
#define TPETIME 13
#define TPETRAN 14
#define TPGOTSIG 15
#define TPERMERR 16
#define TPEITYPE 17
#define TPEOTYPE 18
#define TPERELEASE 19
#define TPEHAZARD 20
#define TPEHEURISTIC 21
#define TPEEVENT 22
#define TPEMATCH 23
#define TPEDIAGNOSTIC 24
#define TPEMIB 25
#define TPMAXVAL 26 /* maximum error message */

/* conversations - events */
#define TPEV_DISCONIMM 0x0001
#define TPEV_SVCERR 0x0002
#define TPEV_SVCFAIL 0x0004
#define TPEV_SVCSUCC 0x0008
#define TPEV_SENDONLY 0x0020

/* /Q diagnostic codes */
#define QMEINVAL -1
#define QMEBADRMID -2
#define QMENOTOPEN -3
#define QMETRAN -4
#define QMEBADMSGID -5
#define QMESYSTEM -6
#define QMEOS -7
#define QMEABORTED -8
#define QMENOTA QMEABORTED
#define QMEPROTO -9
#define QMEBADQUEUE -10
#define QMENOMSG -11
#define QMEINUSE -12
#define QMENOSPACE -13
#define QMERELEASE -14
#define QMEINVHANDLE -15
#define QMESHARE -16

/* EventBroker Messages */
#define TPEVSERVICE 0x00000001
#define TPEVQUEUE 0x00000002
#define TPEVTRAN 0x00000004
#define TPEVPERSIST 0x00000008

/* Subscription Control Structure */
struct tpevctl_t {
  long flags;
  char name1[XATMI_SERVICE_NAME_LENGTH];
  char name2[XATMI_SERVICE_NAME_LENGTH];
  TPQCTL qctl;
};
typedef struct tpevctl_t TPEVCTL;
