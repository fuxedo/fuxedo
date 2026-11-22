// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// Platform environment: the calling-convention macros the classic
// headers and sources use, with K&R-era prototype helpers.

#ifndef FUXEDO_TMENV_H
#define FUXEDO_TMENV_H

#include <sys/types.h>
#include <unistd.h>

/* calling convention and portability macros (no-ops on this port) */
#define _TMDLLENTRY
#define _TM_FAR
#define _TM_FARFUNC
#define _TM_PASCAL

/* K&R prototype helper: _(args) is the argument list */
#ifndef _
#define _(a) a
#endif

/* tpsetunsol(3c) returns the previous handler; TPUNSOLERR is the
 * error sentinel the classic sources compare against */
#define TPUNSOLERR ((void (*)(char *, long, long))-1)

#endif
