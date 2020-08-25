#pragma once

#include "fml32.h"

#ifdef __cplusplus
extern "C" {
#endif
int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags);
#ifdef __cplusplus
}
#endif
#define SRVCNM ((FLDID32)83886081)  // SRVCNM	1	string	     -
#define TA_OPERATION \
  ((FLDID32)83886181)                  // TA_OPERATION	101	string
                                       // -
#define TA_CLASS ((FLDID32)83886182)   // TA_CLASS	102	string	     -
#define TA_CURSOR ((FLDID32)83886183)  // TA_CURSOR	103	string	     -
#define TA_OCCURS ((FLDID32)16777320)  // TA_OCCURS	104	long	       -
#define TA_FLAGS ((FLDID32)16777321)   // TA_FLAGS	105	long	       -
#define TA_FILTER ((FLDID32)16777322)  // TA_FILTER	106	long	       -
#define TA_MIBTIMEOUT \
  ((FLDID32)16777323)  // TA_MIBTIMEOUT	107	long	       -
#define TA_CURSORHOLD \
  ((FLDID32)16777324)                 // TA_CURSORHOLD	108	long	       -
#define TA_MORE ((FLDID32)16777325)   // TA_MORE	109	long	       -
#define TA_ERROR ((FLDID32)16777326)  // TA_ERROR	110	long	       -
// Possible TA_ERROR values
// TAOK the operation was successfully performed. No updates were made to the
// application. TAUPDATED an update was successfully made to the application.
// TAPARTIAL a partial update was successfully made to the application.
#define TAOK 0
#define TAUPDATED 1
#define TAPARTIAL 2
#define TA_SRVGRP ((FLDID32)83886191)  // TA_SRVGRP	111	string	     -
#define TA_GRPNO ((FLDID32)16777328)   // TA_GRPNO	112	long	       -
#define TA_LMID ((FLDID32)83886193)    // TA_LMID	113	string	     -
#define TA_STATE ((FLDID32)83886194)   // TA_STATE	114	string	     -
#define TA_OPENINFO \
  ((FLDID32)83886195)  // TA_OPENINFO	115	string	     -
#define TA_CLOSEINFO \
  ((FLDID32)83886196)  // TA_CLOSEINFO	116	string
                       // -
#define TA_TMSCOUNT \
  ((FLDID32)16777333)                   // TA_TMSCOUNT	117	long	       -
#define TA_TMSNAME ((FLDID32)83886198)  // TA_TMSNAME	118	string	     -
#define TA_APPQMSGID \
  ((FLDID32)83886199)  // TA_APPQMSGID	119	string
                       // -
#define TA_APPQNAME \
  ((FLDID32)83886200)  // TA_APPQNAME	120	string	     -
#define TA_APPQSPACENAME \
  ((FLDID32)83886201)                    // TA_APPQSPACENAME	121	string	     -
#define TA_QMCONFIG ((FLDID32)83886202)  // TA_QMCONFIG	122	string	     -
#define TA_NEWAPPQNAME \
  ((FLDID32)83886203)  // TA_NEWAPPQNAME	123	string	     -
#define TA_PRIORITY \
  ((FLDID32)16777340)                // TA_PRIORITY	124	long	       -
#define TA_TIME ((FLDID32)83886205)  // TA_TIME	125	string	     -
#define TA_EXPIRETIME \
  ((FLDID32)83886206)                  // TA_EXPIRETIME	126	string	     -
#define TA_CORRID ((FLDID32)83886207)  // TA_CORRID	127	string	     -
#define TA_PERSISTENCE \
  ((FLDID32)83886208)  // TA_PERSISTENCE	128	string	     -
#define TA_REPLYPERSISTENCE \
  ((FLDID32)83886209)  // TA_REPLYPERSISTENCE	129	string	     -
#define TA_LOWPRIORITY \
  ((FLDID32)16777346)  // TA_LOWPRIORITY	130	long	       -
#define TA_HIGHPRIORITY \
  ((FLDID32)16777347)  // TA_HIGHPRIORITY	131	long	       -
#define TA_MSGENDTIME \
  ((FLDID32)83886212)  // TA_MSGENDTIME	132	string	     -
#define TA_MSGSTARTTIME \
  ((FLDID32)83886213)  // TA_MSGSTARTTIME	133	string	     -
#define TA_MSGEXPIREENDTIME \
  ((FLDID32)83886214)  // TA_MSGEXPIREENDTIME	134	string	     -
#define TA_MSGEXPIRESTARTTIME \
  ((FLDID32)83886215)  // TA_MSGEXPIRESTARTTIME	135	string	     -
#define TA_CURRETRIES \
  ((FLDID32)16777352)                        // TA_CURRETRIES	136	long	       -
#define TA_MSGSIZE ((FLDID32)16777353)       // TA_MSGSIZE	137	long	       -
#define TA_MSGDATA ((FLDID32)100663434)      // TA_MSGDATA	138	carray	     -
#define TA_SRVID ((FLDID32)16777355)         // TA_SRVID	139	long	 -
#define TA_RMID ((FLDID32)16777356)          // TA_RMID	140	long	 -
#define TA_XID ((FLDID32)83886221)           // TA_XID	141	string	 -
#define TA_IPCKEY ((FLDID32)16777358)        // TA_IPCKEY	142	long
#define TA_MAXACCESSERS ((FLDID32)16777359)  // TA_MAXACCESSERS	143	long
#define TA_MAXGROUPS ((FLDID32)16777360)     // TA_MAXGROUPS	144	long
#define TA_MAXSERVERS ((FLDID32)16777361)    // TA_MAXSERVERS	145	long
#define TA_MAXSERVICES ((FLDID32)16777362)   // TA_MAXSERVICES	146	long
#define TA_MAXQUEUES ((FLDID32)16777363)     // TA_MAXQUEUES	147	long
#define TA_XAFUNC ((FLDID32)16777416)        // TA_XAFUNC	200	long
#define TA_XARET ((FLDID32)16777417)         // TA_XARET	201	long
