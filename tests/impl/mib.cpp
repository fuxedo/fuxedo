// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <sstream>
#include <stdexcept>

#include <tpadm.h>

#include "../../src/fux.h"
#include "../../src/mib.h"
#include "../../src/ubb2mib.h"
#include "../../src/ubbreader.h"

TEST_CASE("ubb2mib propagates restart parameters", "[mib]") {
  setenv("TUXCONFIG", "/app/tuxconfig", 1);
  std::istringstream in(R"(*RESOURCES
IPCKEY 32769
MASTER tuxapp
MODEL SHM

*MACHINES
"localhost" LMID=tuxapp APPDIR="/app" TUXCONFIG="/app/tuxconfig" TUXDIR="/fux"

*GROUPS
GROUP1 LMID=tuxapp GRPNO=1

*SERVERS
pinger SRVGRP=GROUP1 SRVID=1 MIN=1 MAX=1 RESTART=Y MAXGEN=3 GRACE=7
idler  SRVGRP=GROUP1 SRVID=2 MIN=1 MAX=1
)");
  ubbreader reader(in);
  auto u = reader.parse();

  fux::cfg::domain domain;
  domain.ipckey = std::stoul(u.resources["IPCKEY"]);
  domain.maxservers = 10;
  domain.maxservices = 10;
  domain.maxgroups = 10;
  domain.maxqueues = 10;
  mib m(domain, fux::mib::in_heap());
  ubb2mib(u, m);

  auto servers = m.servers();
  auto &pinger = servers.at(m.find_server(1, 1));
  REQUIRE(pinger.restart == true);
  REQUIRE(pinger.maxgen == 3);
  REQUIRE(pinger.grace == 7);
  REQUIRE(pinger.generation == 1);
  REQUIRE(pinger.restart_count == 0);

  auto &idler = servers.at(m.find_server(2, 1));
  REQUIRE(idler.restart == false);
  REQUIRE(idler.maxgen == 1);
  REQUIRE(idler.grace == 86400);
}

TEST_CASE("dead server advertisements are dropped", "[mib]") {
  fux::cfg::domain domain;
  domain.ipckey = 0;
  domain.maxservers = 5;
  domain.maxservices = 5;
  domain.maxgroups = 5;
  domain.maxqueues = 5;

  mib m(domain, fux::mib::in_heap());
  auto srv1 = m.make_server(1, 1, "one", "clopt", "rq1");
  auto q1 = m.servers().at(srv1).rqaddr;
  auto srv2 = m.make_server(2, 1, "two", "clopt", "rq2");
  auto q2 = m.servers().at(srv2).rqaddr;

  m.advertise("svc-a", q1, srv1);
  m.advertise("svc-b", q1, srv1);
  m.advertise("svc-a", q2, srv2);

  m.cleanup_server_advertisements(srv1);

  auto advertisements = m.advertisements();
  size_t alive = 0;
  for (size_t i = 0; i < advertisements.length(); i++) {
    auto &a = advertisements.at(i);
    if (a.state != state_t::INValid) {
      alive++;
      REQUIRE(a.server == srv2);
    }
  }
  REQUIRE(alive == 1);

  // the restart re-advertises without a duplicate error
  m.advertise("svc-a", q1, srv1);
  m.advertise("svc-b", q1, srv1);
}

TEST_CASE("MAXGEN budget is a sliding window of GRACE seconds", "[mib]") {
  server s = {};
  s.maxgen = 2;
  s.grace = 10;
  s.generation = 1;

  REQUIRE(s.consume_restart_slot(100) == true);
  REQUIRE(s.generation == 2);
  REQUIRE(s.consume_restart_slot(105) == true);
  REQUIRE(s.generation == 3);
  // budget exhausted inside the window
  REQUIRE(s.consume_restart_slot(106) == false);
  REQUIRE(s.generation == 3);
  // window slides forward after GRACE seconds
  REQUIRE(s.consume_restart_slot(111) == true);
  REQUIRE(s.generation == 4);
  REQUIRE(s.restart_window_start == 111);
}

TEST_CASE("services can be advertised", "[mib]") {
  fux::cfg::domain domain;
  domain.ipckey = 0;
  domain.maxservers = 5;
  domain.maxservices = 5;
  domain.maxgroups = 5;
  domain.maxqueues = 5;

  mib m(domain, fux::mib::in_heap());
  auto srv = m.make_server(1, 1, "server", "clopt", "rqaddr");
  auto q = m.servers().at(srv).rqaddr;

  m.advertise("service", q, srv);
  REQUIRE_THROWS_AS(m.advertise("service", q, srv), std::logic_error);
  m.unadvertise("service", q, srv);
  REQUIRE_THROWS_AS(m.unadvertise("service", q, srv), std::logic_error);
}

SCENARIO("servers can be added", "[mib]") {
  GIVEN("configuration with max 1 server") {
    fux::cfg::domain domain;
    domain.ipckey = 0;
    domain.maxservers = 1;
    domain.maxservices = 1;
    domain.maxgroups = 1;
    domain.maxqueues = 1;
    mib m(domain, fux::mib::in_heap());

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

TEST_CASE("tpadmcall rejects a NULL output buffer", "[mib]") {
  fux::fml32buf in;
  in.put(TA_CLASS, 0, "T_SERVER");
  in.put(TA_OPERATION, 0, "GET");
  FBFR32 *out = nullptr;
  REQUIRE(tpadmcall(in.ptr(), &out, 0) == -1);
  REQUIRE(tperrno == TPEINVAL);
}
