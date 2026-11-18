// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#pragma once
// Domains configuration model (DMCONFIG/BDMCONFIG). Field semantics and
// defaults pinned against Oracle Tuxedo 12.2 dmloadcf/dmunloadcf.

#include <map>
#include <string>
#include <vector>

namespace fux::cfg {

struct dm_local_domain {
  std::string accesspoint;
  std::string type = "TDOMAIN";
  std::string accesspointid;
  std::string gwgrp;
  std::string dmtlogdev;
  std::string dmtlogname = "DMTLOG";
  long dmtlogsize = 100;
  long blocktime = 6;
  long maxtran = 100;
  std::string connection_policy = "ON_STARTUP";
  std::map<std::string, std::string> extra;
};

struct dm_remote_domain {
  std::string accesspoint;
  std::string type = "TDOMAIN";
  std::string accesspointid;
  std::map<std::string, std::string> extra;
};

struct dm_tdomain_addr {
  std::string accesspoint;
  std::string nwaddr;
  std::string connection_policy;
  std::map<std::string, std::string> extra;
};

struct dm_export_entry {
  std::string servicename;
  std::string laccesspoint;
  std::map<std::string, std::string> extra;
};

struct dm_import_entry {
  std::string servicename;
  std::string laccesspoint;
  std::string raccesspoint;
  std::string rname;
  std::map<std::string, std::string> extra;
};

struct dmconfig {
  std::vector<dm_local_domain> locals;
  std::vector<dm_remote_domain> remotes;
  std::vector<dm_tdomain_addr> tdomain;
  std::vector<dm_export_entry> exports;
  std::vector<dm_import_entry> imports;
};

// Parses DMCONFIG text; throws basic_parser_error on syntax problems.
dmconfig dm_parse(std::istream &in);

// Reads a BDMCONFIG file (dmloadcf's magic+text format) and parses the
// embedded text; throws std::runtime_error on I/O or format problems.
dmconfig dm_load_file(const std::string &path);

// Normalized DMCONFIG text, the shape dmunloadcf prints.
std::string dm_emit(const dmconfig &cfg);

}  // namespace fux::cfg
