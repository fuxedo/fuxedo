// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

#include "mib.h"

int main(int argc, char *argv[]) {
  bool show_help = false;
  std::string file;

  auto parser = clara::Help(show_help) |
                clara::Arg(file, "tuxconfig")("configuration file to load");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || result.value().type() != clara::ParseResultType::Matched) {
    std::cerr << parser;
    return -1;
  }

  try {
    auto cfg = getconfig();

    mib m(cfg);
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
