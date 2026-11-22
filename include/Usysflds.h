// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// System field identifiers for the 16-bit FML flavor. The numbers are
// the platform's Usysflds values: field id = (type << 13) | number.

#ifndef FUXEDO_USYSFLDS_H
#define FUXEDO_USYSFLDS_H

#include <fml.h>

#define INITMSK ((FLDID)40961)    /* number: 1  type: string */
#define CURSID ((FLDID)8194)      /* number: 2  type: long */
#define CURSOC ((FLDID)8195)      /* number: 3  type: long */
#define LEVKEY ((FLDID)40964)     /* number: 4  type: string */
#define STATLIN ((FLDID)40965)    /* number: 5  type: string */
#define FORMNAM ((FLDID)40966)    /* number: 6  type: string */
#define UPDTMOD ((FLDID)7)        /* number: 7  type: short */
#define SRVCNM ((FLDID)40968)     /* number: 8  type: string */
#define NEWFORM ((FLDID)40969)    /* number: 9  type: string */
#define CHGATTS ((FLDID)49162)    /* number: 10 type: carray */
#define USYS1FLD ((FLDID)40971)   /* number: 11 type: string */
#define USYS2FLD ((FLDID)40972)   /* number: 12 type: string */
#define USYS3FLD ((FLDID)40973)   /* number: 13 type: string */
#define USYS4FLD ((FLDID)49166)   /* number: 14 type: carray */
#define USYS5FLD ((FLDID)49167)   /* number: 15 type: carray */
#define USYS6FLD ((FLDID)49168)   /* number: 16 type: carray */
#define DESTSRVC ((FLDID)40977)   /* number: 17 type: string */
#define MODS ((FLDID)49170)       /* number: 18 type: carray */
#define VALONENTRY ((FLDID)40979) /* number: 19 type: string */
#define EVT_MONEVTNONTXNUM ((FLDID)8243)
#define EVT_MONEVTTXNUM ((FLDID)8244)
#define EVT_MONEVTSVCNUM ((FLDID)8245)
#define EVT_MONEVTUNSOLNUM ((FLDID)8246)
#define EVT_MONEVTQUENUM ((FLDID)8247)
#define EVT_MONEVTCMDNUM ((FLDID)8248)
#define EVT_MONEVTULOGNUM ((FLDID)8249)
#define EVT_CROSSDOMAINNAME ((FLDID)41018)
#define TUX_RTNCHAR ((FLDID)16455)
#define TUX_RTNSHORT ((FLDID)72)
#define TUX_RTNLONG ((FLDID)8265)
#define TUX_RTNFLOAT ((FLDID)24650)
#define TUX_RTNDOUBLE ((FLDID)32843)
#define BQCMD ((FLDID)41041)
#define TPMQ_ADAPTER_ERR ((FLDID)41051)
#define TPMQ_ADAPTER_ERR_CODE ((FLDID)41052)
#define TPMQ_APP_ERR ((FLDID)41053)

#endif
