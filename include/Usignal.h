// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// Signal portability surface for the classic applications: USIGTYP
// and the Usignal() installer map onto POSIX <signal.h>.

#ifndef FUXEDO_USIGNAL_H
#define FUXEDO_USIGNAL_H

#include <signal.h>

#ifdef __cplusplus
#define USIGTYP void
extern "C" {
#else
#define USIGTYP void
#endif

#define USIG_ERR SIG_ERR

/* the classic signature; a macro keeps it a plain signal(2) call */
#define Usignal(sig, func) signal((sig), (func))

#ifdef __cplusplus
}
#endif

#endif
