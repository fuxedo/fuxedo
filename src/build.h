// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <map>
#include <string>

#include <unistd.h>

#include "misc.h"

struct rminfo {
  std::string xaswitch;
  std::string lflags;
};

static std::map<std::string, rminfo> parse_rm(const std::string &filename) {
  std::map<std::string, rminfo> rms;
  std::ifstream fin(filename);
  std::string line;
  while (fin) {
    fin >> line;
    if (line.empty() || line.at(0) == '#') {
      continue;
    }
    auto parts = fux::split(line, ":");
    rms.insert(std::make_pair(parts.at(0), rminfo{parts.at(1), parts.at(2)}));
  }
  return rms;
}

static std::map<std::string, rminfo> parse_rm() {
  const char *tuxdir = std::getenv("TUXDIR");
  if (tuxdir == nullptr) {
    tuxdir = "";
  }
  return parse_rm(std::string(tuxdir) + "/share/RM");
}
