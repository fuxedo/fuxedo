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
