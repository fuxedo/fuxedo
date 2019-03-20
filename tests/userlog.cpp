// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <userlog.h>
#include <catch.hpp>

TEST_CASE("test", "[userlog]") { userlog("Hello %s", "world"); }
