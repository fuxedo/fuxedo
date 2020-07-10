// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <stdexcept>

#include "../src/qm.h"

TEST_CASE("openinfo", "[/q]") {
  REQUIRE(tuxq_switch->xa_open_entry("", 1, 0) == TMER_INVAL);
  REQUIRE(tuxq_switch->xa_open_entry("/tmp", 1, 0) == TMER_INVAL);
  REQUIRE(tuxq_switch->xa_open_entry("/tmp:q", 1, 0) == TMER_INVAL);
  REQUIRE(tuxq_switch->xa_close_entry(nullptr, 1, 0) == XA_OK);
}
