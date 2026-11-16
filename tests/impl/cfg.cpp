// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <sstream>
#include <stdexcept>

#include "../../src/cfg.h"
#include "../../src/ubbreader.h"

namespace {

const char *UBB = R"(*RESOURCES
IPCKEY 32769
MASTER tuxapp
MODEL SHM
MAXSERVERS 10

*MACHINES
"localhost" LMID=tuxapp APPDIR="/app" TUXCONFIG="/app/tuxconfig" TUXDIR="/fux"

*GROUPS
GROUP1 LMID=tuxapp GRPNO=1
GROUP2 LMID=tuxapp GRPNO=2 TMSNAME=TMS_YEA OPENINFO="yea:stuff"

*SERVERS
pinger SRVGRP=GROUP1 SRVID=1 MIN=2 MAX=2 REPLYQ=Y RQADDR=ping \
       MAXGEN=2 RESTART=Y GRACE=0
)";

fux::cfg::tables parse(const std::string &text) {
  std::istringstream in(text);
  ubbreader p(in);
  return fux::cfg::build(p.parse());
}

}  // namespace

TEST_CASE("ubbconfig text builds into typed tables", "[cfg]") {
  auto t = parse(UBB);

  REQUIRE(t.dom.ipckey == 32769);
  REQUIRE(t.dom.master == "tuxapp");
  REQUIRE(t.dom.model == "SHM");
  REQUIRE(t.dom.maxservers == 10);
  REQUIRE(t.dom.maxservices == 100);

  REQUIRE(t.mach.lmid == "tuxapp");
  REQUIRE(t.mach.appdir == "/app");

  REQUIRE(t.groups.size() == 2);
  REQUIRE(t.groups[1].grpno == 2);
  REQUIRE(t.groups[1].tmsname == "TMS_YEA");
  REQUIRE(t.groups[1].openinfo == "yea:stuff");

  REQUIRE(t.servers.size() == 1);
  REQUIRE(t.servers[0].servername == "pinger");
  REQUIRE(t.servers[0].srvid == 1);
  REQUIRE(t.servers[0].min == 2);
  REQUIRE(t.servers[0].max == 2);
  REQUIRE(t.servers[0].replyq == true);
  REQUIRE(t.servers[0].rqaddr == "ping");
  REQUIRE(t.servers[0].maxgen == 2);
  REQUIRE(t.servers[0].restart == true);
  REQUIRE(t.servers[0].grace == 0);
}

TEST_CASE("emitted text parses back to equal tables", "[cfg]") {
  auto t = parse(UBB);
  auto text = fux::cfg::emit(t);
  auto t2 = parse(text);

  REQUIRE(t2.dom.ipckey == t.dom.ipckey);
  REQUIRE(t2.dom.master == t.dom.master);
  REQUIRE(t2.dom.maxservers == t.dom.maxservers);
  REQUIRE(t2.mach.appdir == t.mach.appdir);
  REQUIRE(t2.groups == t.groups);
  REQUIRE(t2.servers == t.servers);
}

TEST_CASE("defaults are applied", "[cfg]") {
  auto t = parse(R"(*RESOURCES
IPCKEY 1

*MACHINES
"m" LMID=l

*GROUPS
G LMID=l GRPNO=1

*SERVERS
s SRVGRP=G SRVID=5
)");
  REQUIRE(t.servers[0].min == 1);
  REQUIRE(t.servers[0].max == 1);
  REQUIRE(t.servers[0].clopt == "-A");
  REQUIRE(t.servers[0].rqaddr == "1.5");
  REQUIRE(t.servers[0].grace == 86400);
  REQUIRE(t.servers[0].maxgen == 1);
  REQUIRE(t.servers[0].restart == false);
  REQUIRE(t.servers[0].replyq == true);  // REPLYQ=Y by default now
  REQUIRE(t.dom.maxaccessers == 50);
}

TEST_CASE("validation rejects bad input", "[cfg]") {
  REQUIRE_THROWS_AS(parse(""), std::out_of_range);  // no IPCKEY
  REQUIRE_THROWS_AS(parse("*RESOURCES\nIPCKEY abc\n"), std::out_of_range);
  REQUIRE_THROWS_AS(parse(R"(*RESOURCES
IPCKEY 1

*MACHINES
a LMID=l
b LMID=l
)"),
                    std::logic_error);  // two machines
  REQUIRE_THROWS_AS(parse(R"(*RESOURCES
IPCKEY 1

*MACHINES
m LMID=l

*SERVERS
s SRVGRP=G SRVID=1
)"),
                    std::out_of_range);  // SRVID required... group missing
}
