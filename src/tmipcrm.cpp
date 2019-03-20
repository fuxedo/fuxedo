// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

#include "mib.h"

int main(int argc, char *argv[]) {
  bool show_help = false;

  auto parser = clara::Parser() | clara::Help(show_help);

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }

  try {
    auto cfg = getconfig();

    mib m(cfg);
    m.remove();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
