#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <cstdint>
#include <string>
#include <vector>

#include "ubb.h"

namespace fux::cfg {

struct domain {
  uint32_t ipckey = 0;
  std::string master;
  std::string model = "SHM";
  std::string domainid;
  std::string ldbal = "N";
  uint16_t maxaccessers = 50;
  uint16_t maxservers = 50;
  uint16_t maxservices = 100;
  uint16_t maxgroups = 100;
  uint16_t maxqueues = 50;
};

struct machine {
  std::string pmid;
  std::string lmid;
  std::string appdir;
  std::string tuxconfig;
  std::string tuxdir;
  std::string ulogpfx;
  std::string tlogdevice;
  long blocktime = 0;  // milliseconds
};

struct group {
  std::string srvgrp;
  std::string lmid;
  uint16_t grpno = 0;
  std::string openinfo;
  std::string closeinfo;
  std::string tmsname;

  bool operator==(const group &o) const {
    return srvgrp == o.srvgrp && lmid == o.lmid && grpno == o.grpno &&
           openinfo == o.openinfo && closeinfo == o.closeinfo &&
           tmsname == o.tmsname;
  }
};

struct server {
  std::string srvgrp;
  std::string servername;
  uint16_t srvid = 0;
  uint16_t min = 1;
  uint16_t max = 1;
  uint16_t mindispatchthreads = 1;
  uint16_t maxdispatchthreads = 1;
  std::string clopt = "-A";
  std::string envfile;
  std::string dependson;
  std::string rcmd;
  std::string rqaddr;
  long grace = 86400;
  uint16_t maxgen = 1;
  long threadstacksize = 0;
  bool restart = false;
  bool replyq = true;  // REPLYQ=N unsupported for now
  bool secondaryrq = false;
  bool autostart = true;

  bool operator==(const server &o) const {
    return srvgrp == o.srvgrp && servername == o.servername &&
           srvid == o.srvid && min == o.min && max == o.max &&
           mindispatchthreads == o.mindispatchthreads &&
           maxdispatchthreads == o.maxdispatchthreads && clopt == o.clopt &&
           envfile == o.envfile && dependson == o.dependson &&
           rcmd == o.rcmd && rqaddr == o.rqaddr && grace == o.grace &&
           maxgen == o.maxgen && threadstacksize == o.threadstacksize &&
           restart == o.restart && replyq == o.replyq &&
           secondaryrq == o.secondaryrq && autostart == o.autostart;
  }
};

struct tables {
  domain dom;
  machine mach;
  std::vector<group> groups;
  std::vector<server> servers;
};

// Parses and validates a ubbconfig into typed tables. Throws
// std::out_of_range/std::logic_error with a message on invalid input.
tables build(const ubbconfig &ubb);

// Serializes tables back to UBBCONFIG text. Output is regenerated and
// normalized: only modeled fields are emitted, original comments and
// unknown parameters are dropped.
std::string emit(const tables &t);

}  // namespace fux::cfg
