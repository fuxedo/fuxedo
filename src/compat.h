#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "config.h"

namespace fux::compat {

// Oracle Tuxedo generation this build emulates: 121 or 221. Selected at
// configure time with --with-tuxedo-compat=12.1|22.1 (default 12.1); one
// mode per build, never per process. Divergence points are pinned in
// private/notes/compat-mode.md.
inline constexpr int tux_version = FUXEDO_TUX_COMPAT;
inline constexpr bool is221 = tux_version == 221;

}  // namespace fux::compat
