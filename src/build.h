// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

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
  while (getline(fin, line)) {
    if (line.empty() || line.at(0) == '#') {
      continue;
    }
    auto parts = fux::split(line, ":");
    rms.insert(std::make_pair(parts.at(0), rminfo{parts.at(1), parts.at(2)}));
  }
  return rms;
}

static std::map<std::string, rminfo> parse_rm() {
  auto tuxdir = fux::util::getenv("TUXDIR", "");
  return parse_rm(tuxdir + "/share/RM");
}
