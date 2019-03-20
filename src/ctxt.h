#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <xa.h>

namespace fux {
class call_descriptors;

namespace glob {

xa_switch_t *xaswitch();
fux::call_descriptors *calldescs();
}  // namespace glob
}  // namespace fux
