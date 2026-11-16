// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <stdlib.h>
#include <userlog.h>
#include <catch.hpp>

TEST_CASE("test", "[userlog]") { userlog("Hello %s", "world"); }
TEST_CASE("test millisec", "[userlog]") {
  setenv("ULOGMILLISEC", "y", 1);
  userlog("Hello %s", "world in milliseconds");
}
TEST_CASE("test prefix", "[userlog]") {
  setenv("ULOGPFX", "/root/no_permission", 1);
  userlog("Hello %s", " no permission to root");
}
