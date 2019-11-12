// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <stdexcept>

#include "../src/mib.h"
#include "../src/ubbreader.h"

TEST_CASE("services can be advertised", "[mib]") {
  tuxconfig tuxcfg;
  tuxcfg.size = 0;
  tuxcfg.ipckey = 0;
  tuxcfg.maxservers = 5;
  tuxcfg.maxservices = 5;
  tuxcfg.maxgroups = 5;
  tuxcfg.maxqueues = 5;

  mib m(tuxcfg, fux::mib::in_heap());
  auto srv = m.make_server(1, 1, "server", "clopt", "rqaddr");
  auto q = m.servers().at(srv).rqaddr;

  m.advertise("service", q, srv);
  REQUIRE_THROWS_AS(m.advertise("service", q, srv), std::logic_error);
  m.unadvertise("service", q, srv);
  REQUIRE_THROWS_AS(m.unadvertise("service", q, srv), std::logic_error);
}

SCENARIO("servers can be added", "[mib]") {
  GIVEN("configuration with max 1 server") {
    tuxconfig tuxcfg;
    tuxcfg.size = 0;
    tuxcfg.ipckey = 0;
    tuxcfg.maxservers = 1;
    tuxcfg.maxservices = 1;
    tuxcfg.maxgroups = 1;
    tuxcfg.maxqueues = 1;
    mib m(tuxcfg, fux::mib::in_heap());

    WHEN("one server is added") {
      auto ptr = m.make_server(1, 1, "server", "clopt", "rqaddr");

      THEN("it can be found") { REQUIRE(ptr == m.find_server(1, 1)); }
      THEN("values are stored") {
        //        REQUIRE(ptr != mib::badoff);
        REQUIRE(strcmp(m.servers().at(ptr).servername, "server") == 0);
        REQUIRE(strcmp(m.servers().at(ptr).clopt, "clopt") == 0);
      }
      AND_THEN("queue has been registered") {
        REQUIRE(m.servers().at(ptr).rqaddr == m.find_queue("rqaddr"));
      }
      THEN("duplicate can't be added") {
        REQUIRE_THROWS_AS(m.make_server(1, 1, "server", "clopt", "rqaddr"),
                          std::logic_error);
      }
      THEN("max limit can't be exceeded") {
        REQUIRE_THROWS_AS(m.make_server(2, 2, "server", "clopt", "rqaddr"),
                          std::out_of_range);
      }
    }
  }
}
