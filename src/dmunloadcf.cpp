// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <string>

#include "dmcfg.h"
#include "misc.h"

// Prints the normalized DMCONFIG for a BDMCONFIG, like dmunloadcf.

int main(int argc, char **argv) {
  bool show_help = false;
  std::vector<std::string> extra;
  auto parser = clara::Help(show_help) | clara::Arg(extra, "ignored")("");
  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || show_help) {
    std::cerr << parser;
    return !result ? -1 : 1;
  }
  auto path = fux::util::getenv("BDMCONFIG", "");
  if (path.empty()) {
    std::cerr << "dmunloadcf: BDMCONFIG environment variable not set"
              << std::endl;
    return 1;
  }
  try {
    auto cfg = fux::cfg::dm_load_file(path);
    std::cout << fux::cfg::dm_emit(cfg);
  } catch (const std::exception &e) {
    std::cerr << "dmunloadcf: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
