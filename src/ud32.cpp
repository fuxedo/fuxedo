// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "extreader.h"

int main(int argc, char *argv[]) {
  /*
  bool show_help = false;

  std::vector<std::string> files;


  auto parser =
      clara::Help(show_help) +
      clara::Arg(files, "field_table")("field tables to process");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || result.value().type() != clara::ParseResultType::Matched) {
    std::cerr << parser;
    return -1;
  }
  */

  try {
    extreader r(stdin);
    r.parse();
  } catch (const std::system_error &e) {
    std::cerr << e.code().message() << std::endl;
    return -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
