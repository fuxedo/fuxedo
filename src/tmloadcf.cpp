// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <system_error>

#include "cfg.h"
#include "mib.h"
#include "ubbreader.h"

#include "misc.h"

#ifdef HAVE_CXX_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

void ubb2mib(ubbconfig &u, mib &m);

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

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-") == 0) {
      argv[i] = const_cast<char *>("/dev/stdin");
    }
  }

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || result.value().type() != clara::ParseResultType::Matched) {
    std::cerr << parser;
    return -1;
  }
  if (show_help) {
    std::cout << parser;
    return 0;
  }

  auto outfile = std::getenv("TUXCONFIG");
  if (outfile == nullptr) {
    std::cerr << "TUXCONFIG environment variable required" << std::endl;
    return -1;
  }

  try {
    std::string text;
    if (file == "-") {
      // rewritten to /dev/stdin above
    }
    // a lone "-" is rewritten to /dev/stdin before parsing
    std::ifstream fin;
    fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fin.open(file);
    std::ostringstream src;
    src << fin.rdbuf();
    text = src.str();

    std::istringstream in(text);
    ubbreader p(in);
    auto config = p.parse();

    auto tables = fux::cfg::build(config);

    mib m(tables.dom, fux::mib::in_heap());
    ubb2mib(config, m);

    tuxconfig hdr = {};
    memcpy(hdr.magic, TUXCONFIG_MAGIC, sizeof(hdr.magic));
    hdr.hdr_size = sizeof(hdr);
    hdr.version = TUXCONFIG_VERSION;
    hdr.text_off = sizeof(hdr);
    hdr.text_size = text.size();
    hdr.ipckey = tables.dom.ipckey;
    hdr.flags = TUXCONFIG_FLAG_LOADED;
    hdr.revision = 1;

    std::ofstream fout(outfile, std::ios::binary);
    fout.write(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    fout.write(text.data(), text.size());
  } catch (const std::system_error &e) {
    std::cerr << e.code().message() << std::endl;
    return -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
