// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>

extern FLDID32 FUX_SRVID;
extern FLDID32 FUX_GRPNO;

namespace fux {
namespace tm {
enum xa_func { prepare, commit, rollback };
extern FLDID32 FUX_XAFUNC;
extern FLDID32 FUX_XAXID;
extern FLDID32 FUX_XARMID;
extern FLDID32 FUX_XAFLAGS;
extern FLDID32 FUX_XARET;
}  // namespace tm
}  // namespace fux
