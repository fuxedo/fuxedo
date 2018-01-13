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

#include <tpadm.h>
#include <xatmi.h>

int tpenqueue(char *qspace, char *qname, TPQCTL *ctl, char *data, long len,
              long flags) {
  return -1;
}
void tpforward(char *svc, char *data, long len, long flags) {}
int tpadmcall(FBFR32 *inbuf, FBFR32 **outbuf, long flags) { return -1; }
