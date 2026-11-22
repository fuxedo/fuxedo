// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// 16-bit FML ("FML16"), the classic fielded-buffer flavor used by
// long-lived applications (bankapp among them). Same programming
// model as FML32 with 16-bit field identifiers and no nested FML32.
// Only what real applications use is implemented; the layout is
// fuxedo-internal (both ends of a call are fuxedo-built), the API
// and semantics follow the platform. Like the platform, the calls
// answer 1 on success and -1 on error with Ferror set; write new
// code as != -1, never as a boolean test.

#ifndef FUXEDO_FML_H
#define FUXEDO_FML_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLD_SHORT 0
#define FLD_LONG 1
#define FLD_CHAR 2
#define FLD_FLOAT 3
#define FLD_DOUBLE 4
#define FLD_STRING 5
#define FLD_CARRAY 6

#define FLD_MAX 14

#ifndef BADFLDID
#define FMLTYPE "FML"
#define VIEWTYPE "VIEW"

#define BADFLDID (FLDID)0
#endif
#ifndef FIRSTFLDID
#define FIRSTFLDID (FLDID)0
#endif
#ifndef FIRSTFLD
#define FIRSTFLD FIRSTFLDID
#endif

/* field id: value type in the top 3 bits, number in the low 13 */
#define Fmkfldid(type, num) \
  (FLDID)((((unsigned short)(type)&0x7) << 13) | ((num)&0x1FFF))
#define Fldtype(id) ((int)(((unsigned short)(id) >> 13) & 0x7))
#define Fldno(id) ((long)((unsigned short)(id)&0x1FFF))

typedef unsigned short FLDID;
typedef unsigned short FLDLEN;
typedef int FLDOCC;

struct fbfr;
typedef struct fbfr FBFR;
struct Fbfr32;
typedef struct Fbfr32 FBFR32;

extern int Ferror;

/* operations used by the classic applications (bankapp et al) */
extern int Fadd(FBFR *fb, FLDID fieldid, char *value, FLDLEN len);
extern int Fchg(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value,
                FLDLEN len);
extern int Fdel(FBFR *fb, FLDID fieldid, FLDOCC occ);
extern FLDOCC Fdelall(FBFR *fb, FLDID fieldid);
extern char *Ffind(FBFR *fb, FLDID fieldid, FLDOCC occ, FLDLEN *len);
extern int Fget(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value,
                FLDLEN *len);
extern char *Fvals(FBFR *fb, FLDID fieldid, FLDOCC occ);
extern long Fvall(FBFR *fb, FLDID fieldid, FLDOCC occ);
extern long Fsizeof(FBFR *fb);
extern long Fused(FBFR *fb);
extern FLDOCC Fcount(FBFR *fb, FLDID fieldid);
extern FLDOCC Foccur(FBFR *fb, FLDID fieldid);
extern int Finit(FBFR *fb, FLDLEN buflen);
extern FBFR *Falloc(FLDOCC flds, FLDLEN len);
extern void Ffree(FBFR *fb);
extern const char *Fstrerror(int err);

/* counted-length variants carry a user type for conversion checks; the
 * fuxedo records are already typed, so these mirror the plain calls */
extern int CFadd(FBFR *fb, FLDID fieldid, char *value, FLDLEN len,
                 int usrdtyp);
extern int CFchg(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value,
                 FLDLEN len, int usrdtyp);
extern char *CFfind(FBFR *fb, FLDID fieldid, FLDOCC occ, FLDLEN *len,
                    int usrdtyp);
extern int CFget(FBFR *fb, FLDID fieldid, FLDOCC occ, char *value,
                 FLDLEN *len, int usrdtyp);

/* name-based lookup shares the FML32 field tables */
extern FLDID Fldid(char *name);

/* in-place converters between the 16-bit and 32-bit flavors, the
 * platform's (dest, src) shape */
extern int F16to32(FBFR32 *dest, FBFR *src);
extern int F32to16(FBFR *dest, FBFR32 *src);

/* error codes mirroring the platform numbers */
#define FMINERR -1
#ifndef FALIGNERR
#define FALIGNERR 1
#endif
#define FNOTFLD 2
#ifndef FNOSPACE
#define FNOSPACE 3
#endif
#ifndef FNOTPRES
#define FNOTPRES 4
#endif
#ifndef FBADFLD
#define FBADFLD 5
#endif
#ifndef FTYPERR
#define FTYPERR 6
#endif
#define FEUNIX 7
#define FBADNAME 8
#ifndef FMALLOC
#define FMALLOC 9
#endif
#define FALIGNMENT 10
#define FOPEN 14
#ifndef FSYNTAX
#define FSYNTAX 15
#endif
#define FTOPEN 16
#define FSTAT 17
#define FEINVALIDOPCODE 20
#define FEEBADACC 21
#ifndef FEINVAL
#define FEINVAL 22
#endif

#ifdef __cplusplus
}
#endif

#endif
