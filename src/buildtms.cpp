// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your /ption) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <clara.hpp>
#include <iostream>
#include <string>

#include <unistd.h>

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool verbose = false;

  std::string outfile;
  std::string rmname;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(verbose)["-v"]("write compilation command to stdout") |
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

  char command[4 * 1024];
  snprintf(command, sizeof(command),
           "$TUXDIR/bin/buildserver -r \"%s\" -o \"%s\" -f "
           "$TUXDIR/lib/tmserver.o -s .TM:TM%s",
           rmname.c_str(), outfile.c_str(), verbose ? " -v" : "");
  if (verbose) {
    std::cout << command << std::endl;
  }

  return std::system(command);
}
