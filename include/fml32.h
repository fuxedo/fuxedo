#pragma once
#include <stdint.h>
#include <stdio.h>

typedef uint32_t FLDID32;
typedef uint32_t FLDLEN32;
typedef int32_t FLDOCC32;
typedef struct Fbfr32 FBFR32;

// Use the same numbers as Tuxedo for the same ordering of fields :-(
#define FLD_SHORT 0
#define FLD_LONG 1
#define FLD_CHAR 2
#define FLD_FLOAT 3
#define FLD_DOUBLE 4
#define FLD_STRING 5
#define FLD_CARRAY 6
#define FLD_PTR 9
#define FLD_FML32 10

#define BADFLDID ((FLDID32)0)
#define FIRSTFLDID ((FLDID32)0)

#define FALIGN 1
#define FNOTFLD 2
#define FNOSPACE 3
#define FNOTPRES 4
#define FBADFLD 5
#define FTYPERR 6
#define FEUNIX 7
#define FBADNAME 8
#define FMALLOC 9
#define FSYNTAX 10
#define FFTOPEN 11
#define FFTSYNTAX 12
#define FEINVAL 13
#define FBADTBL 14
#define FBADVIEW 15
#define FVFSYNTAX 16
#define FVFOPEN 17
#define FBADACM 18
#define FNOCNAME 19
#define FEBADOP 20
#define FNOTRECORD 21
#define FRFSYNTAX 22
#define FRFOPEN 23
#define FBADRECORD 24

#ifdef __cplusplus
extern "C" {
#endif

int *_tls_Ferror32();
#define Ferror32 (*_tls_Ferror32())
char *Fstrerror32(int err);

long Fneeded32(FLDOCC32, FLDLEN32);
FLDID32 Fmkfldid32(int type, FLDID32 num);
int Fldtype32(FLDID32 fieldid);
long Fldno32(FLDID32 fieldid);
char *Fname32(FLDID32 fieldid);
FLDID32 Fldid32(char *name);
void Fidnm_unload32();
void Fnmid_unload32();
FBFR32 *Falloc32(FLDOCC32 F, FLDLEN32 V);
FBFR32 *Frealloc32(FBFR32 *fbfr, FLDOCC32 F, FLDLEN32 V);
int Ffree32(FBFR32 *fbfr);
int Finit32(FBFR32 *fbfr, FLDLEN32 buflen);
long Fsizeof32(FBFR32 *fbfr);
long Fused32(FBFR32 *fbfr);
long Funused32(FBFR32 *fbfr);
long Fidxused32(FBFR32 *fbfr);
int Findex32(FBFR32 *fbfr, FLDOCC32 intvl);
int Funindex32(FBFR32 *fbfr);
int Frstrindex32(FBFR32 *fbfr, FLDOCC32 numidx);
int Fchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *value,
           FLDLEN32 len);
char *Ffind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len);
char *Ffindlast32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 *oc, FLDLEN32 *len);
int Fget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc,
           FLDLEN32 *len);
int Fpres32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
FLDOCC32 Foccur32(FBFR32 *fbfr, FLDID32 fieldid);
long Flen32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
int Fprint32(FBFR32 *fbfr);
int Ffprint32(FBFR32 *fbfr, FILE *iop);
int Fdel32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
int Fdelall32(FBFR32 *fbfr, FLDID32 fieldid);
int Fadd32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len);
int Fnext32(FBFR32 *fbfr, FLDID32 *fieldid, FLDOCC32 *oc, char *value,
            FLDLEN32 *len);
int Fcpy32(FBFR32 *dest, FBFR32 *src);
char *Fgetalloc32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc,
                  FLDLEN32 *extralen);
int Fdelete32(FBFR32 *fbfr, FLDID32 *fieldid);
int Fproj32(FBFR32 *fbfr, FLDID32 *fieldid);
int Fupdate32(FBFR32 *dest, FBFR32 *src);
int Fjoin32(FBFR32 *dest, FBFR32 *src);
int Fojoin32(FBFR32 *dest, FBFR32 *src);
long Fchksum32(FBFR32 *fbfr);
char *Ftypcvt32(FLDLEN32 *tolen, int totype, char *fromval, int fromtype,
                FLDLEN32 fromlen);

FLDOCC32 Ffindocc32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len);
int Fconcat32(FBFR32 *dest, FBFR32 *src);
int Fprojcpy32(FBFR32 *dest, FBFR32 *src, FLDID32 *fieldid);

char *Fboolco32(char *expression);
void Fboolpr32(char *tree, FILE *iop);
int Fboolev32(FBFR32 *fbfr, char *tree);
double Ffloatev32(FBFR32 *fbfr, char *tree);

char *CFfind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len,
               int type);
int CFadd32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len, int type);
int CFchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *value,
            FLDLEN32 len, int type);
FLDOCC32
CFfindocc32(FBFR32 *fbfr, FLDID32 fieldid, char *value, FLDLEN32 len, int type);
int CFget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *buf,
            FLDLEN32 *len, int type);

char *Ffinds32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
int Fgets32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *buf);

int Fextread32(FBFR32 *fbfr, FILE *iop);
int Fwrite32(FBFR32 *fbfr, FILE *iop);
int Fread32(FBFR32 *fbfr, FILE *iop);

#ifdef __cplusplus
}
#endif
