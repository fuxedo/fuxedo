// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// Portability and environment helpers. Provides the environment
// accessor functions and the U* operation identifiers used with the
// Uunix error reporting helpers.

#ifndef FUX_UUNIX_H
#define FUX_UUNIX_H

#ifndef TMENV_H
#include <tmenv.h>
#endif

#include <float.h>
#include <limits.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* identifiers for the operations reported through Uunix_err() */
#define UUNIXMIN 0
#define UCLOSE 1
#define UCREAT 2
#define UEXEC 3
#define UFCTNL 4
#define UFORK 5
#define ULSEEK 6
#define UMSGCTL 7
#define UMSGGET 8
#define UMSGSND 9
#define UMSGRCV 10
#define UOPEN 11
#define UPLOCK 12
#define UREAD 13
#define USEMCTL 14
#define USEMGET 15
#define USEMOP 16
#define USHMCTL 17
#define USHMGET 18
#define USHMAT 19
#define USHMDT 20
#define USTAT 21
#define UWRITE 22
#define USBRK 23
#define USYSMUL 24
#define UWAIT 25
#define UKILL 26
#define UTIME 27
#define UMKDIR 28
#define ULINK 29
#define UUNLINK 30
#define UUNAME 31
#define UNLIST 32
#define UUNIXMAX 33

extern int *Uunixerr_addr(void);
#define Uunixerr (*Uunixerr_addr())

extern void Uunix_err(const char *opname);
extern const char *Uunixmsg[];
extern char *Ustrerror(int err);

extern char *tuxgetenv(const char *name);
extern int tuxputenv(const char *str);
extern int tuxreadenv(const char *file, const char *prefix);

#ifdef __cplusplus
}
#endif

/* alignment and rounding helpers */
#define GP_ROUNDUP(amt, sz) \
  ((((size_t)(amt) + (size_t)(sz)-1) / (size_t)(sz)) * (size_t)(sz))
#define GP_ROUNDUP4(amt) (((size_t)(amt) + 3u) & ~(size_t)3u)
#define GP_ROUNDUP8(amt) (((size_t)(amt) + 7u) & ~(size_t)7u)

#define BITSPERBYTE 8
#define MAXSHORT SHRT_MAX
#define MINSHORT SHRT_MIN
#define MAXINT INT_MAX
#define MININT INT_MIN
#define MAXLONG LONG_MAX
#define MINLONG LONG_MIN

#ifndef MAXFLOAT
#define MAXFLOAT FLT_MAX
#endif
#ifndef MAXDOUBLE
#define MAXDOUBLE DBL_MAX
#endif

#define ALIGN_LONG(x) (x)

#endif
