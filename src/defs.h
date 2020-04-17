#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <limits>

namespace fux {
// Global Transaction Table ID
typedef uint16_t gttid;
const gttid bad_gttid = std::numeric_limits<gttid>::max();
};  // namespace fux
