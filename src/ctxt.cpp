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

#include "ctxt.h"
#include "calldesc.h"

namespace fux {
namespace glob {

xa_switch_t *xa_switch;
xa_switch_t *xaswitch() { return xa_switch; }

fux::call_descriptors *calldescs() {
  static fux::call_descriptors cds;
  return &cds;
}
}  // namespace glob
}  // namespace fux
