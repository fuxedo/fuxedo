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
