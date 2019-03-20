// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#include "mib.h"
#include "ubbreader.h"

#include "misc.h"

void ubb2mib(ubbconfig &u, mib &m);

template <typename T>
long require(T &dict, const std::string &key, long min, long max,
             long defvalue) {
  if (dict.find(key) == dict.end()) {
    return defvalue;
    //    throw std::out_of_range(key + " required");
  }
  auto value = std::stol(dict[key]);
  if (value < min || value > max) {
    throw std::out_of_range(key + " out of range");
  }
  return value;
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool noop = false;
  bool yes = false;
  bool count = false;
  std::string file;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(noop)["-n"]("perform syntax check, do not load") |
      clara::Opt(yes)["-y"]("answer Yes to all questions") |
      clara::Opt(count)["-c"]("print IPC resources needed for configuration") |
      clara::Arg(file, "ubbconfig")("configuration file to load").required();

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || result.value().type() != clara::ParseResultType::Matched) {
    std::cerr << parser;
    return -1;
  }

  auto outfile = std::getenv("TUXCONFIG");
  if (outfile == nullptr) {
    std::cerr << "TUXCONFIG environment variable required" << std::endl;
    return -1;
  }

  ubbconfig config;
  try {
    std::ifstream fin;
    fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fin.open(file);

    ubbreader p(fin);
    config = p.parse();

    tuxconfig tuxcfg;
    tuxcfg.size = sizeof(tuxcfg) + filesize(file);
    tuxcfg.ipckey = std::stoi(config.resources.at("IPCKEY"));
    tuxcfg.maxservers = require(config.resources, "MAXSERVERS", 1, 8192, 50);
    tuxcfg.maxservices =
        require(config.resources, "MAXSERVICES", 1, 1048575, 100);
    tuxcfg.maxgroups = require(config.resources, "MAXGROUPS", 1, 32768, 100);
    tuxcfg.maxqueues = require(config.resources, "MAXQUEUES", 1, 8192, 50);
    tuxcfg.maxaccessers =
        require(config.resources, "MAXACCESSERS", 1, 32768, 100);
    /*
    TUXCONFIG = string_value[2..256] (up to 64 bytes for Oracle Tuxedo 8.0 or
    earlier)
      This is the absolute pathname of the file or device where the binary
    TUXCONFIG file is found on this machine. The administrator need only
    maintain one TUXCONFIG file, namely the one that is pointed to by the
    TUXCONFIG environment variable on the MASTER machine. Copies on other
    machines of this master TUXCONFIG file are synchronized with the MASTER
    machine automatically when the system is booted. This parameter must be
    specified for each machine. If TUXOFFSET is specified, the Oracle Tuxedo
    filesystem starts at that number of blocks from the beginning of the
    TUXCONFIG device (see TUXOFFSET below). See ENVFILE in the MACHINES section
    for a discussion of how this value is used in the environment.
      Note:
      The pathname specified for this parameter must match exactly (including
    case) the pathname specified for the TUXCONFIG environment variable.
    Otherwise, tmloadcf(1) cannot be run successfully. To use the Shared
    Applications Staging feature in MP mode, users must set different TUXCONFIG
    values on different nodes.
      TUXDIR = string_value[2..256] (up to 78 bytes for Oracle Tuxedo 8.0 or
    earlier)
      This is the absolute pathname of the directory where the Oracle Tuxedo
    system software is found on this machine. This parameter must be specified
    for each machine and the pathname should be local to each machine; in other
    words, TUXDIR should not be on a remote filesystem. If the machines of a
    multiprocessor application have different Oracle Tuxedo system releases
    installed, check the Oracle Tuxedo Release Notes for the higher level
    release to make sure you will get the functionality you expect. See ENVFILE
    in the MACHINES section for a discussion of how this value is used in the
    environment.
      APPDIR = string_value[2..256] (up to 78 bytes for Oracle Tuxedo 8.0 or
    earlier)
      The value specified for this parameter is the absolute pathname of the
    application directory and is the current directory for all application and
    administrative servers booted on this machine. The absolute pathname can
    optionally be followed by a colon-separated list of other pathnames. In a
    configuration where SECURITY is set while neither EXALOGIC option nor MP
    mode is set, each application must have its own distinct APPDIR. See ENVFILE
    in the MACHINES section for a discussion of how this value is used in the
    environment.

      */

    std::ofstream fout(outfile, std::ios::binary);
    fout.write(reinterpret_cast<char *>(&tuxcfg), sizeof(tuxcfg));

    fin.clear();
    fin.seekg(0);
    fout << fin.rdbuf();

    //    tuxcfg.maxqueues = std::stoi(config.resources.at("MAXQUEUES"));

    mib m(tuxcfg, fux::mib::in_heap());

    ubb2mib(config, m);
  } catch (const std::system_error &e) {
    std::cerr << e.code().message() << std::endl;
    return -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
