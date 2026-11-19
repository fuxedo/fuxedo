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
#include <unistd.h>

#include "cfg.h"
#include "compat.h"
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
  long noop_blocks = 0;
  long noop_blocks2 = 0;
  std::string file;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(noop)["-n"]("perform syntax check, do not load") |
      clara::Opt(yes)["-y"]("answer Yes to all questions") |
      clara::Opt(count)["-c"]("print IPC resources needed for configuration") |
      clara::Opt(noop_blocks, "blocks")["-b"](
          "TUXCONFIG size in blocks (accepted for compatibility; the fuxedo "
          "TUXCONFIG does not need preallocation)") |
      clara::Opt(noop_blocks2, "blocks")["-w"](
          "maximum blocks before prompting (accepted for compatibility; "
          "ignored)") |
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
    std::ifstream fin;
    fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fin.open(file);
    std::ostringstream src;
    src << fin.rdbuf();
    std::string text = src.str();

    std::istringstream in(text);
    ubbreader p(in);
    auto config = p.parse();

    auto tables = fux::cfg::build(config);

    if (fux::compat::is221) {
      // 22.x requires SECURITY in *RESOURCES and refuses to load on a
      // non-master node; 12.x defaults SECURITY and loads anywhere
      if (config.resources.count("SECURITY") == 0) {
        std::cerr << "tmloadcf: SECURITY must be specified in the RESOURCES "
                     "section"
                  << std::endl;
        return -1;
      }
      if (config.resources.at("SECURITY") == "NONE") {
        std::cerr << "tmloadcf: warning: SECURITY NONE is an insecure option"
                  << std::endl;
      }
      if (config.resources.count("MAXACCESSERS") == 0 ||
          config.resources.count("MAXSERVERS") == 0) {
        std::cerr << "tmloadcf: warning: MAXACCESSERS/MAXSERVERS not "
                     "specified, defaults apply"
                  << std::endl;
      }
      if (tables.dom.master != tables.mach.lmid) {
        std::cerr << "tmloadcf: cannot run on a non-master node" << std::endl;
        return -1;
      }
    }

    if (count) {
      fux::cfg::print_sizing(std::cout, tables);
      return 0;
    }

    if (!noop) {
      // An application booted from this configuration keeps its bulletin
      // board in shared memory; refuse to overwrite while it is alive and
      // clean up a leftover board from a dead one.
      switch (mib::check_node(tables.dom.ipckey)) {
        case node_state::running:
          std::cerr << "tmloadcf: an application is already running with "
                       "this TUXCONFIG"
                    << std::endl;
          return -1;
        case node_state::not_running:
        case node_state::stale_removed:
          break;
      }

      if (!yes && fs::exists(outfile) && isatty(STDIN_FILENO)) {
        std::cout << "Overwrite TUXCONFIG file " << outfile << " [y, q]? ";
        std::cout.flush();
        std::string answer;
        std::getline(std::cin, answer);
        if (answer.empty() || answer[0] != 'y') {
          std::cerr << "tmloadcf: TUXCONFIG file not updated" << std::endl;
          return 0;
        }
      }

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
      // mode 0600, like the platform tools
      fs::permissions(outfile,
                      fs::perms::owner_read | fs::perms::owner_write);
    }

    if (noop) {
      mib m(tables.dom, fux::mib::in_heap());
      ubb2mib(config, m);
    }
  } catch (const std::system_error &e) {
    std::cerr << e.code().message() << std::endl;
    return -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
