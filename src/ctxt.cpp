// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

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
