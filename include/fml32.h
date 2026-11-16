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

int Fvftos32(FBFR32 *fbfr, char *cstruct, char *viewname);
int Fvstof32(FBFR32 *fbfr, char *cstruct, int mode, char *viewname);
int Fvnull32(char *cstruct, char *cname, FLDOCC32 oc, char *viewname);
int Fvsinit32(char *cstruct, char *viewname);
int Fvselinit32(char *cstruct, char *cname, char *viewname);
int Fvopt32(char *cname, int option, char *viewname);
int Fvneeded32(char *viewname);
void Fvrefresh32();
char *Fvboolco32(char *expression, char *viewname);
int Fvboolev32(char *cstruct, char *tree, char *viewname);
double Fvfloatev32(char *cstruct, char *tree, char *viewname);
int Fvboolpr32(char *tree, FILE *iop, char *viewname);

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

#define BLOCKBUFSIZE 4096
#define BUFSIZE 512
#define C_BOOL 14
#define C_CARRAY 6 /* character array */
#define C_CHAR 2 /* character */
#define C_DECIMAL 8 /* 8 reserved for FLD_DECIMAL (see viewmap.h) */
#define C_DOUBLE 4 /* double-precision float */
#define C_FLOAT 3 /* single-precision float */
#define C_FML 13 /* for embedded ptr to FML buffer */
#define C_FML32 10 /* embedded FML32 buffer */
#define C_INT 7 /* 7 reserved for FLD_INT (see viewmap.h) */
#define C_LDOUBLE 22
#define C_LLONG 20
#define C_LONG 1 /* long int */
#define C_MBSTRING 12 /* embedded multibyte codeset info */
#define C_NUMTYPE 24
#define C_PTR 9 /* pointer */
#define C_SCHAR 16
#define C_SHORT 0 /* short int */
#define C_STRING 5 /* string - null terminated */
#define C_UCHAR 15
#define C_UINT 18
#define C_ULLONG 21
#define C_ULONG 19
#define C_USHORT 23
#define C_VIEW32 11 /* embedded VIEW32 */
#define C_WCHAR 17
#define FADD 1
#define FBUFENC 1
#define FCONCAT 2
#define FDEL 3
#define FJOIN 3
#define FLD_FML 13 /* for embedded ptr to FML buffer */
#define FLD_MAX 14 /* for field type validation */
#define FLD_MBSTRING 12 /* embedded multibyte codeset info */
#define FLD_VIEW32 11 /* embedded VIEW32 */
#define FLD_XPATH 14 /* for xpath */
#define FMAXNULLSIZE 2660
#define FMAXVAL 25 /* top of error message codes */
#define FMINVAL 0 /* bottom of error message codes */
#define FML32_H 
#define FMLMOD 2
#define FMLTYPE32 "FML32"
#define FOJOIN 4
#define FRECORDCACHESIZE 128
#define FRECORDNAMESIZE 33
#define FSTDXINT 16 /* Default indexing interval */
#define FTERMENC 2
#define FUPDATE 1
#define FVIEWCACHESIZE 128
#define FVIEWFLDOVHD FVIEWNAMESIZE
#define FVIEWNAMESIZE 33
#define F_BOTH (F_STOF | F_FTOS) /* S,F */
#define F_COUNT 64 /* C */
#define F_FTOS 8 /* S */
#define F_LENGTH 32 /* L */
#define F_NONE 128 /* NONE flag for null value */
#define F_OFF 0 /* Z */
#define F_OFFSET 1
#define F_PROP 4 /* P */
#define F_SIZE 2
#define F_STOF 16 /* F */
#define Fread32(A,B) FMLread32(A,B)
#define Fwrite32(A,B) FMLwrite32(A,B)
#define MAXFBLEN32 0x7ffffffe /* Maximum FBFR3232 length */
#define MINBUFSIZE 256
#define RCONFLICTOK 1
#define RECORDTYPE "RECORD"
#define ROK 0
#define TPENC_ASCII 0x01 /* character encoding is ASCII format */
#define TPENC_BIG_ENDIAN 0x04 /* big endian */
#define TPENC_EBCDIC 0x02 /* character encoding is EBCDIC format */
#define TPENC_LITTLE_ENDIAN 0x08 /* little endian */
#define TPENC_MAINFRAME_FLOAT 0x10 /* mainframe float*/
#define TPXMLALLOWEMPTY 0x2000 /* None parser flag, ATMI use only */
#define TPXPARSALWAYS 0x0002
#define TPXPARSCACHERESET 0x0400
#define TPXPARSCACHESET 0x0200
#define TPXPARSCONFATAL 0x0008
#define TPXPARSDOSCH 0x0020
#define TPXPARSEREFN 0x0040
#define TPXPARSNEVER 0x0001
#define TPXPARSNOEXIT 0x0080
#define TPXPARSNOINCWS 0x0100
#define TPXPARSNSPACE 0x0010
#define TPXPARSSCHFULL 0x0004
#define TPXPARSSTDURI 0x0800 /* TPXPARSFILE use only */
#define TPXPARSUSECACHEDGRAM 0x1000 /* TPXPARSFILE use only */
#define VIEWTYPE32 "VIEW32"

#define FMLread32(x, y) Fread32(x, y)
#define FMLwrite32(x, y) Fwrite32(x, y)

#ifdef __cplusplus
}
#endif
