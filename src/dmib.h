#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>

// Admin protocol between dmadmin and the gateway's DMADM_<accesspoint>
// service: the tpadm DM MIB field numbers, composed directly so no
// field table has to be loaded.
namespace dmib {
inline FLDID32 operation() { return Fmkfldid32(FLD_STRING, 6012); }
inline FLDID32 accesspoint() { return Fmkfldid32(FLD_STRING, 30001501); }
inline FLDID32 accesspointid() { return Fmkfldid32(FLD_STRING, 30001502); }
inline FLDID32 nwaddr() { return Fmkfldid32(FLD_STRING, 6776); }
inline FLDID32 policy() { return Fmkfldid32(FLD_STRING, 6917); }
inline FLDID32 state() { return Fmkfldid32(FLD_STRING, 6014); }
}  // namespace dmib
