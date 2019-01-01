#pragma once

#include "fml32.h"

#ifdef __cplusplus
extern "C" {
#endif
int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags);
#ifdef __cplusplus
}
#endif
#define TA_OPERATION	((FLDID32)83886181)		// TA_OPERATION	101	string	 -
#define TA_CLASS	((FLDID32)83886182)		// TA_CLASS	102	string	 -
#define TA_CURSOR	((FLDID32)83886183)		// TA_CURSOR	103	string	 -
#define TA_OCCURS	((FLDID32)16777320)		// TA_OCCURS	104	long	 -
#define TA_FLAGS	((FLDID32)16777321)		// TA_FLAGS	105	long	 -
#define TA_FILTER	((FLDID32)16777322)		// TA_FILTER	106	long	 -
#define TA_MIBTIMEOUT	((FLDID32)16777323)		// TA_MIBTIMEOUT	107	long	 -
#define TA_CURSORHOLD	((FLDID32)16777324)		// TA_CURSORHOLD	108	long	 -
#define TA_MORE	((FLDID32)16777325)		// TA_MORE	109	long	 -
#define TA_ERROR	((FLDID32)16777326)		// TA_ERROR	110	long	 -
// Possible TA_ERROR values
// TAOK the operation was successfully performed. No updates were made to the application.
// TAUPDATED an update was successfully made to the application.
// TAPARTIAL a partial update was successfully made to the application.
#define TAOK       0
#define TAUPDATED  1
#define TAPARTIAL  2
#define TA_SRVGRP	((FLDID32)83886191)		// TA_SRVGRP	111	string	 -
#define TA_GRPNO	((FLDID32)16777328)		// TA_GRPNO	112	long	 -
#define TA_LMID	((FLDID32)83886193)		// TA_LMID	113	string	 -
#define TA_STATE	((FLDID32)83886194)		// TA_STATE	114	string	 -
#define TA_OPENINFO	((FLDID32)83886195)		// TA_OPENINFO	115	string	 -
#define TA_CLOSEINFO	((FLDID32)83886196)		// TA_CLOSEINFO	116	string	 -
#define TA_TMSCOUNG	((FLDID32)16777333)		// TA_TMSCOUNG	117	long	 -
#define TA_TMSNAME	((FLDID32)83886198)		// TA_TMSNAME	118	string	 -
