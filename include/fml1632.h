// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// Coexistence header for translation units that include both the
// 16-bit and the 32-bit FML flavors (the classic samples do).
// Conversions between the flavors follow the platform's in-place
// shape: the caller provides the destination buffer.

#ifndef FUXEDO_FML1632_H
#define FUXEDO_FML1632_H

#include <fml.h>
#include <fml32.h>

#ifdef __cplusplus
extern "C" {
#endif

int F16to32(FBFR32 *dest, FBFR *src);
int F32to16(FBFR *dest, FBFR32 *src);

#ifdef __cplusplus
}
#endif

#endif
