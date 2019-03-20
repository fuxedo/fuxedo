// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <iostream>
#include <string>
#include <vector>

#include <cstdlib>

#include "misc.h"

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool verbose = false;

  std::string outfile;
  std::string rmname;

  std::vector<std::string> firstfiles;
  std::vector<std::string> lastfiles;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(verbose)["-v"]("write compilation command to stdout") |
      clara::Opt(firstfiles, "firstfiles")["-f"](
          "compilation parameters to include before Fuxedo libraries") |
      clara::Opt(lastfiles, "lastfiles")["-l"](
          "compilation parameters to include after Fuxedo libraries") |
      clara::Opt(rmname, "rmname")["-r"]("resource manager name") |
      clara::Opt(outfile, "outfile")["-o"]("output file name");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  if (show_help) {
    std::cout << parser;
    return 0;
  }

  auto tuxdir = fux::util::getenv("TUXDIR", "");
  auto cc = fux::util::getenv("CC", "cc");
  auto cflags = fux::util::getenv("CFLAGS", "");

  std::vector<std::string> cmd;
  cmd.push_back(cc);
  cmd.push_back(std::string("-I") + tuxdir + "/include");
  cmd.insert(cmd.end(), firstfiles.begin(), firstfiles.end());
  cmd.push_back(std::string("-L") + tuxdir + "/lib");
  cmd.push_back("-lfuxedo");
  cmd.push_back("-lpthread");
  cmd.push_back("-o");
  cmd.push_back(outfile);
  cmd.insert(cmd.end(), lastfiles.begin(), lastfiles.end());

  auto command = join(cmd, " ");

  if (verbose) {
    std::cout << command << std::endl;
  }

  return std::system(command.c_str());
}
