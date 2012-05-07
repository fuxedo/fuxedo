#ifndef __fml32_h
#define __fml32_h

#include <stdint.h>

typedef uint32_t FLDID32;
typedef uint32_t FLDLEN32;
typedef int32_t FLDOCC32;
typedef struct Fbfr32 FBFR32;

#define FLD_SHORT 0
#define FLD_LONG 1
#define FLD_CHAR 2
#define FLD_FLOAT 3
#define FLD_DOUBLE 4
#define FLD_STRING 5
#define FLD_CARRAY 6
#define FLD_PTR 9
#define FLD_FML32 10
#define FLD_VIEW32 11
#define FLD_MBSTRING 12
#define FLD_FML 13

#define BADFLDID ((FLDID32)0)
#define FIRSTFLDID ((FLDID32)0)

#define FMINVAL 0
#define FALIGNERR 1
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
#define FMAXVAL 21

#define Ferror32 (*_thread_specific_Ferror32())

#ifdef __cplusplus
extern "C" {
#endif

int *_thread_specific_Ferror32();
char *Fstrerror32(int err);
FLDID32 Fmkfldid32(int type, FLDID32 num);
int Fldtype32(FLDID32 fieldid);
long Fldno32(FLDID32 fieldid);
char *Fname32(FLDID32 fieldid);
FLDID32 Fldid32(char *name);
void Fidnm_unload32();
FBFR32 *Falloc32(FLDOCC32 F, FLDLEN32 V);
int Ffree32(FBFR32 *fbfr);
int Finit32(FBFR32 *fbfr, FLDLEN32 buflen);
long Fsizeof32(FBFR32 *fbfr);
long Fused32(FBFR32 *fbfr);
long Fidxused32(FBFR32 *fbfr);
int Findex32(FBFR32 *fbfr, FLDOCC32 intvl);
int Funindex32(FBFR32 *fbfr);
int Frstrindex32(FBFR32 *fbfr, FLDOCC32 numidx);
int Fchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *value, FLDLEN32 len);
char *Ffind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len);
int Fget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc, FLDLEN32 *len);
int Fpres32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
FLDOCC32 Foccur32(FBFR32 *fbfr, FLDID32 fieldid);
long Flen32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
int Fprint32(FBFR32 *fbfr);
int Ffprint32(FBFR32 *fbfr, FILE *iop);
int Fdel32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc);
int Fdelall32(FBFR32 *fbfr, FLDID32 fieldid);

#ifdef __cplusplus
}
#endif

#if 0
extern int _TMDLLENTRY Fread32 _((FBFR32 *, FILE *));
extern int _TMDLLENTRY Fwrite32 _((FBFR32 *, FILE *));
extern int _TMDLLENTRY CFadd32 _((FBFR32 _TM_FAR *, FLDID32, char _TM_FAR*, FLDLEN32, int));
extern int _TMDLLENTRY CFchg32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, char _TM_FAR *, FLDLEN32, int ));
extern char _TM_FAR * _TMDLLENTRY CFfind32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, FLDLEN32 _TM_FAR *, int));
extern FLDOCC32 _TMDLLENTRY CFfindocc32 _((FBFR32 _TM_FAR *, FLDID32, char _TM_FAR *, FLDLEN32, int ));
extern int _TMDLLENTRY CFget32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, char _TM_FAR *, FLDLEN32 _TM_FAR *, int ));
extern char _TM_FAR * _TMDLLENTRY CFgetalloc32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, int, FLDLEN32 _TM_FAR *));
extern void _TMDLLENTRY F_error32 _((char *));
extern int _TMDLLENTRY Fadd32 _((FBFR32 _TM_FAR *, FLDID32, char _TM_FAR *, FLDLEN32));
extern int _TMDLLENTRY Fappend32 _((FBFR32 _TM_FAR *, FLDID32, char _TM_FAR *, FLDLEN32));
extern FBFR32 _TM_FAR * _TMDLLENTRY Frealloc32 _((FBFR32 _TM_FAR *, FLDOCC32, FLDLEN32));
extern int _TMDLLENTRY Fboolev32 _((FBFR32 _TM_FAR *, char _TM_FAR *));
extern int _TMDLLENTRY Fvboolev32 _((char _TM_FAR *, char _TM_FAR *, char _TM_FAR *));
extern double _TMDLLENTRY Ffloatev32 _((FBFR32 _TM_FAR *, char _TM_FAR *));
extern double _TMDLLENTRY Fvfloatev32 _((char _TM_FAR *, char _TM_FAR *, char _TM_FAR *));
extern void _TMDLLENTRY Fboolpr32 _((char _TM_FAR *, FILE _TM_FAR *));
extern int _TMDLLENTRY Fvboolpr32 _((char _TM_FAR *, FILE _TM_FAR *, char _TM_FAR *));
extern long _TMDLLENTRY Fchksum32 _((FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fcmp32 _((FBFR32 _TM_FAR *, FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fconcat32 _((FBFR32 _TM_FAR *, FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fcpy32 _((FBFR32 _TM_FAR *,FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fdelete32 _((FBFR32 _TM_FAR *, FLDID32 _TM_FAR *));
extern int _TMDLLENTRY Fextread32 _((FBFR32 _TM_FAR *, FILE _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Ffind32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, FLDLEN32 _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Fvals32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32 ));
extern long _TMDLLENTRY Fvall32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32 ));
extern FLDOCC32 _TMDLLENTRY Ffindocc32 _((FBFR32 _TM_FAR *, FLDID32, char _TM_FAR *, FLDLEN32 ));
extern char _TM_FAR * _TMDLLENTRY Fgetalloc32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, FLDLEN32 _TM_FAR *));
extern int _TMDLLENTRY Fielded32 _((FBFR32 _TM_FAR *));
extern long _TMDLLENTRY Fneeded32 _((FLDOCC32, FLDLEN32));
extern long _TMDLLENTRY Funused32 _((FBFR32 _TM_FAR *));
extern FLDLEN32 _TMDLLENTRY Fieldlen32 _((char _TM_FAR *, FLDLEN32 _TM_FAR *, FLDLEN32 _TM_FAR *));
extern int _TMDLLENTRY Fjoin32 _((FBFR32 _TM_FAR *, FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fojoin32 _((FBFR32 _TM_FAR *, FBFR32 _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Ffindlast32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32 _TM_FAR *, FLDLEN32 _TM_FAR *));
extern int _TMDLLENTRY Fgetlast32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32 _TM_FAR *, char _TM_FAR *, FLDLEN32 _TM_FAR *));
extern int _TMDLLENTRY Fmbpack32 _((char _TM_FAR *, void _TM_FAR *, FLDLEN32, void _TM_FAR *, FLDLEN32 _TM_FAR *, long));
extern int _TMDLLENTRY Fmbunpack32 _((void _TM_FAR *, FLDLEN32, char _TM_FAR *, void _TM_FAR *, FLDLEN32 _TM_FAR *, long));
extern int _TMDLLENTRY Fmove32 _((char _TM_FAR *, FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fnext32 _((FBFR32 _TM_FAR *, FLDID32 _TM_FAR *, FLDOCC32 _TM_FAR *, char _TM_FAR *, FLDLEN32 _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Ftype32 _((FLDID32));
extern void _TMDLLENTRY Fnmid_unload32 _((void));
extern FLDOCC32 _TMDLLENTRY Fnum32 _((FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fproj32 _((FBFR32 _TM_FAR *, FLDID32 _TM_FAR *));
extern int _TMDLLENTRY Fprojcpy32 _((FBFR32 _TM_FAR *, FBFR32 _TM_FAR *, FLDID32 _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Ftypcvt32 _((FLDLEN32 _TM_FAR *, int, char _TM_FAR *, int, FLDLEN32 ));
extern int _TMDLLENTRY Fupdate32 _((FBFR32 _TM_FAR *, FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fvneeded32 _((char _TM_FAR *));
extern int _TMDLLENTRY Fvopt32 _((char _TM_FAR *, int, char _TM_FAR *));
extern int _TMDLLENTRY Fvsinit32 _((char _TM_FAR *, char _TM_FAR *));
extern int _TMDLLENTRY Fvnull32 _((char _TM_FAR *, char _TM_FAR *, FLDOCC32, char _TM_FAR *));
extern int _TMDLLENTRY Fvselinit32 _((char _TM_FAR *, char _TM_FAR *, char _TM_FAR *));
extern int _TMDLLENTRY Fvftos32 _((FBFR32 _TM_FAR *, char _TM_FAR *, char _TM_FAR *));
extern void _TMDLLENTRY Fvrefresh32 _((void));
extern int _TMDLLENTRY Fvstof32 _((FBFR32 _TM_FAR *, char _TM_FAR *, int, char _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Fboolco32 _((char _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Fvboolco32 _((char _TM_FAR *, char _TM_FAR *));
long _TMDLLENTRY Fvttos32 _((char _TM_FAR *cstruct, char _TM_FAR *trecord, char _TM_FAR *viewname));
long _TMDLLENTRY Fvstot32 _((char _TM_FAR *cstruct, char _TM_FAR *trecord, long treclen, char _TM_FAR *viewname));
extern int _TMDLLENTRY Fcodeset32 _((unsigned char _TM_FAR *codeset));
extern int _TMDLLENTRY Flevels32 _((int));
extern int  _TMDLLENTRY maskprt32 _((FBFR32 _TM_FAR *));
extern int _TMDLLENTRY Fadds32 _((FBFR32 _TM_FAR *, FLDID32, char _TM_FAR *));
extern int _TMDLLENTRY Fchgs32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, char _TM_FAR *));
extern int _TMDLLENTRY Fgets32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, char _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Fgetsa32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32, FLDLEN32 _TM_FAR *));
extern char _TM_FAR * _TMDLLENTRY Ffinds32 _((FBFR32 _TM_FAR *, FLDID32, FLDOCC32));
extern int _TMDLLENTRY _Finitx32 _((FBFR32 *, FLDLEN32, FLDOCC32));
extern int _TMDLLENTRY tpxmltofml32 _((char *, char *, FBFR32 **, char **, long));
extern int _TMDLLENTRY tpfml32toxml _((FBFR32 *, char *, char *, char **, long));
#endif

#endif
