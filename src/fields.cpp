// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>

FLDID32 FUX_SRVID = Fmkfldid32(FLD_LONG, 10);
FLDID32 FUX_GRPNO = Fmkfldid32(FLD_LONG, 11);

namespace fux {
namespace tm {
FLDID32 FUX_XAFUNC = Fmkfldid32(FLD_LONG, 20);
FLDID32 FUX_XAXID = Fmkfldid32(FLD_CARRAY, 21);
FLDID32 FUX_XARMID = Fmkfldid32(FLD_LONG, 22);
FLDID32 FUX_XAFLAGS = Fmkfldid32(FLD_LONG, 23);
FLDID32 FUX_XARET = Fmkfldid32(FLD_LONG, 24);
}  // namespace tm
}  // namespace fux
