// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

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
