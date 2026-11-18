// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <sstream>
#include <stdexcept>

#include "../../src/cfg.h"
#include "../../src/dmcfg.h"
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

TEST_CASE("parameter forms match the Oracle grammar", "[cfg]") {
  // *RESOURCES takes PARAM VALUE (quoted or bare), never PARAM=VALUE
  auto t = parse(R"(*RESOURCES
IPCKEY 32769
MASTER tuxapp
MODEL SHM
MAXACCESSERS 20
MAXSERVERS 5
MAXSERVICES 10
DOMAINID "a b"

*MACHINES
"localhost" LMID=tuxapp APPDIR="/app" TUXCONFIG="/app/tuxconfig" TUXDIR="/fux"

*GROUPS
GROUP1 LMID=tuxapp GRPNO=1

*SERVERS
pinger SRVGRP=GROUP1 SRVID=1 MAXGEN=2 RESTART=Y GRACE=0
)");
  REQUIRE(t.dom.ipckey == 32769);
  REQUIRE(t.dom.domainid == "a b");

  // spaces around '=' are fine in continuation lines
  auto t2 = parse(R"(*RESOURCES
IPCKEY 32769
MASTER tuxapp
MODEL SHM

*MACHINES
"localhost" LMID = tuxapp APPDIR = "/app" TUXCONFIG = "/app/tuxconfig" TUXDIR = "/fux"

*GROUPS
GROUP1 LMID = tuxapp GRPNO = 1

*SERVERS
pinger SRVGRP = GROUP1 SRVID = 1
)");
  REQUIRE(t2.mach.lmid == "tuxapp");
  REQUIRE(t2.groups[0].grpno == 1);
  REQUIRE(t2.servers[0].srvid == 1);

  // PARAM=VALUE in RESOURCES is an Oracle syntax error, not a parse
  REQUIRE_THROWS_AS(parse(R"(*RESOURCES
IPCKEY=32769
MASTER=tuxapp
MODEL=SHM
)"),
                    basic_parser_error);

  // bare KEY VALUE in continuation lines is an Oracle syntax error too
  REQUIRE_THROWS_AS(parse(R"(*RESOURCES
IPCKEY 32769
MASTER tuxapp
MODEL SHM

*MACHINES
"localhost" LMID tuxapp APPDIR="/app" TUXCONFIG="/app/tuxconfig" TUXDIR="/fux"
)"),
                    basic_parser_error);
}

TEST_CASE("dmconfig text parses and emits back", "[cfg]") {
  std::istringstream in(R"(*DM_LOCAL_DOMAINS
DOM1	TYPE=TDOMAIN ACCESSPOINTID="BA.CENTRAL01" GWGRP=GW1 CONNECTION_POLICY=ON_STARTUP

*DM_REMOTE_DOMAINS
DOM2	TYPE=TDOMAIN ACCESSPOINTID="BA.BRANCH01"

*DM_TDOMAIN
DOM1	NWADDR="//localhost:19001"
DOM2	NWADDR="//localhost:19002" CONNECTION_POLICY=ON_STARTUP

*DM_EXPORT
TOUPPER	LACCESSPOINT=DOM1

*DM_IMPORT
TOLOWER	LACCESSPOINT=DOM1 RACCESSPOINT=DOM2 RNAME=TOLOWER
)");
  auto dm = fux::cfg::dm_parse(in);
  REQUIRE(dm.locals.size() == 1);
  REQUIRE(dm.locals[0].accesspoint == "DOM1");
  REQUIRE(dm.locals[0].accesspointid == "BA.CENTRAL01");
  REQUIRE(dm.locals[0].gwgrp == "GW1");
  REQUIRE(dm.locals[0].connection_policy == "ON_STARTUP");
  REQUIRE(dm.locals[0].dmtlogname == "DMTLOG");
  REQUIRE(dm.remotes.size() == 1);
  REQUIRE(dm.tdomain.size() == 2);
  REQUIRE(dm.tdomain[1].connection_policy == "ON_STARTUP");
  REQUIRE(dm.exports.size() == 1);
  REQUIRE(dm.imports.size() == 1);
  REQUIRE(dm.imports[0].rname == "TOLOWER");

  std::istringstream back(fux::cfg::dm_emit(dm));
  auto dm2 = fux::cfg::dm_parse(back);
  REQUIRE(dm2.locals.size() == 1);
  REQUIRE(dm2.imports.size() == 1);
  REQUIRE(dm2.imports[0].raccesspoint == "DOM2");
}
