// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

#include "basic_parser.h"
#include "cfg.h"
#include "dmcfg.h"
#include "misc.h"
#include "tlog.h"
#include "ubb2mib.h"
#include "ubbreader.h"

// Loads a BDMCONFIG from a DMCONFIG text file, the way dmloadcf does.
// The gateway group must exist in the application's TUXCONFIG and carry
// a GWADM server, matching the platform's validation.

static int load(const std::string &text, const std::string &outfile,
                bool syntax_only) {
  std::istringstream in(text);
  auto cfg = fux::cfg::dm_parse(in);

  try {
    fux::cfg::tables tables;
    getconfig(&tables);
    for (auto &dom : cfg.locals) {
      size_t grp = mib::badoff;
      for (size_t i = 0; i < tables.groups.size(); i++) {
        if (tables.groups[i].srvgrp == dom.gwgrp) {
          grp = i;
          break;
        }
      }
      if (grp == mib::badoff) {
        std::cerr << "dmloadcf: group name " << dom.gwgrp
                  << " not defined in TUXCONFIG file" << std::endl;
        return 1;
      }
      bool gwadm = false;
      for (auto &s : tables.servers) {
        if (s.srvgrp == dom.gwgrp && s.servername == "GWADM") {
          gwadm = true;
          break;
        }
      }
      if (!gwadm) {
        std::cerr << "dmloadcf: group name " << dom.gwgrp
                  << " not associated with GWADM server in TUXCONFIG file"
                  << std::endl;
        return 1;
      }
      if (!dom.dmtlogdev.empty()) {
        // like the platform: DMTLOGDEV names an existing transaction log
        // device created with crdl, not a plain path
        try {
          (void)fux::tlog::log::open(dom.dmtlogdev);
        } catch (const std::exception &) {
          std::cerr << "dmloadcf: " << dom.dmtlogdev
                    << " is not a transaction log device (create it with "
                       "crdl first)"
                    << std::endl;
          return 1;
        }
      }
    }
  } catch (const basic_parser_error &e) {
    std::cerr << "dmloadcf: " << e.what() << " " << e.extra << std::endl;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "dmloadcf: " << e.what() << std::endl;
    return 1;
  }

  if (syntax_only) {
    return 0;
  }

  std::ofstream out(outfile, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "dmloadcf: cannot write " << outfile << std::endl;
    return 1;
  }
  // fuxedo BDMCONFIG: small binary header + normalized DMCONFIG text
  struct {
    char magic[8];
    uint32_t hdr_size;
    uint32_t version;
    uint64_t text_size;
  } hdr = {};
  std::string text_out = fux::cfg::dm_emit(cfg);
  memcpy(hdr.magic, "FUXBDM01", 8);
  hdr.hdr_size = sizeof(hdr);
  hdr.version = 1;
  hdr.text_size = text_out.size();
  out.write(reinterpret_cast<char *>(&hdr), sizeof(hdr));
  out.write(text_out.data(), text_out.size());
  return 0;
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool yes = false;
  bool syntax_only = false;
  bool check_running = false;
  std::string infile;
  long blocks = 0;
  long version = 0;
  std::string password;

  auto parser =
      clara::Help(show_help) | clara::Opt(yes)["-y"]("answer Yes to all") |
      clara::Opt(syntax_only)["-n"](
          "syntax check only, do not write the BDMCONFIG") |
      clara::Opt(check_running)["-c"](
          "check that the new configuration can be booted (accepted for "
          "compatibility; fuxedo validates against TUXCONFIG anyway)") |
      clara::Opt(blocks, "blocks")["-b"](
          "BDMCONFIG size in blocks (accepted; the fuxedo BDMCONFIG is "
          "header plus text and does not need preallocation)") |
      clara::Opt(version, "1|2")["-v"](
          "configuration version level (accepted for compatibility)") |
      clara::Opt(password, "password")["-p"](
          "administrative password (accepted; SECURITY NONE ignores it)") |
      clara::Arg(infile, "DMCONFIG_file")("DMCONFIG file (or - for stdin)");
  // a lone "-" means stdin; clara would treat it as an option
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "-" && infile.empty()) {
      infile = "-";
      continue;
    }
    args.push_back(argv[i]);
  }
  std::vector<char *> argv2;
  argv2.push_back(argv[0]);
  for (auto &a : args) {
    argv2.push_back(const_cast<char *>(a.c_str()));
  }

  auto result = parser.parse(clara::Args(argv2.size(), argv2.data()));
  if (!result || show_help) {
    std::cerr << parser;
    return show_help ? 0 : 1;
  }

  std::string text;
  if (infile.empty() || infile == "-") {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    text = ss.str();
  } else {
    std::ifstream in(infile);
    if (!in) {
      std::cerr << "dmloadcf: cannot open " << infile << std::endl;
      return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
  }

  auto outfile = fux::util::getenv("BDMCONFIG", "");
  if (outfile.empty()) {
    std::cerr << "dmloadcf: BDMCONFIG environment variable not set"
              << std::endl;
    return 1;
  }
  if (!syntax_only && !yes) {
    std::ifstream exists(outfile);
    if (exists) {
      std::cout << "Overwrite BDMCONFIG file " << outfile << " [y, q]? "
                << std::flush;
      std::string answer;
      std::getline(std::cin, answer);
      if (answer.empty() || (answer[0] != 'y' && answer[0] != 'Y')) {
        return 1;
      }
    }
  }

  try {
    return load(text, outfile, syntax_only);
  } catch (const basic_parser_error &e) {
    std::cerr << "dmloadcf: " << e.what() << " " << e.extra << std::endl;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "dmloadcf: " << e.what() << std::endl;
    return 1;
  }
}
