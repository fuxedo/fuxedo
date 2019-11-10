#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>
#include <fstream>

#define DECONST(x) const_cast<char *>(x)

struct tempfile {
  tempfile(int line) {
    name = __FILE__ + std::to_string(line) + ".tmp";
    f = fopen(name.c_str(), "w");
    REQUIRE(f != nullptr);
  }

  ~tempfile() { remove(name.c_str()); }
  std::string name;
  FILE *f;
};

static std::string read_file(const std::string &fname) {
  std::ifstream file(fname, std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string buffer(size, '\0');
  if (file.read(&buffer[0], size)) {
    return buffer;
  }
  return "";
}


