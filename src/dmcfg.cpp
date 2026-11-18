// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "dmcfg.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "basic_parser.h"
#include "misc.h"

namespace fux::cfg {

namespace {

class dmreader : public basic_parser {
 public:
  dmreader(std::istream &f) : basic_parser(f) {}

  dmconfig parse() {
    dmconfig cfg;
    std::string cursection, tmp;

    while (true) {
      std::string object;
      if (skip_line()) {
        continue;
      }
      if (section(&tmp)) {
        cursection = tmp;
        tmp.clear();
        continue;
      }
      if (eof()) {
        break;
      }
      if (!token(&object, false)) {
        throw basic_parser_error("entry name expected", row_, "NAME");
      }
      blank();

      params kv;
      kv_list(&kv, true);

      if (cursection == "DM_LOCAL_DOMAINS" || cursection == "DM_LOCAL") {
        auto &e = cfg.locals.emplace_back();
        e.accesspoint = object;
        e.type = get(kv, "TYPE", "TDOMAIN");
        e.accesspointid = get(kv, "ACCESSPOINTID", get(kv, "DOMAINID", ""));
        e.gwgrp = get(kv, "GWGRP", "");
        e.dmtlogdev = get(kv, "DMTLOGDEV", "");
        e.dmtlogname = get(kv, "DMTLOGNAME", "DMTLOG");
        e.dmtlogsize = std::stol(get(kv, "DMTLOGSIZE", "100"));
        e.blocktime = std::stol(get(kv, "BLOCKTIME", "6"));
        e.maxtran = std::stol(get(kv, "MAXTRAN", "100"));
        e.connection_policy = get(kv, "CONNECTION_POLICY", "ON_STARTUP");
        take_extra(kv, &e.extra,
                   {"TYPE", "ACCESSPOINTID", "DOMAINID", "GWGRP", "DMTLOGDEV",
                    "DMTLOGNAME", "DMTLOGSIZE", "BLOCKTIME", "MAXTRAN",
                    "CONNECTION_POLICY"});
      } else if (cursection == "DM_REMOTE_DOMAINS" || cursection == "DM_REMOTE") {
        auto &e = cfg.remotes.emplace_back();
        e.accesspoint = object;
        e.type = get(kv, "TYPE", "TDOMAIN");
        e.accesspointid = get(kv, "ACCESSPOINTID", get(kv, "DOMAINID", ""));
        take_extra(kv, &e.extra, {"TYPE", "ACCESSPOINTID", "DOMAINID"});
      } else if (cursection == "DM_TDOMAIN") {
        auto &e = cfg.tdomain.emplace_back();
        e.accesspoint = object;
        e.nwaddr = get(kv, "NWADDR", "");
        e.connection_policy = get(kv, "CONNECTION_POLICY", "");
        take_extra(kv, &e.extra, {"NWADDR", "CONNECTION_POLICY"});
      } else if (cursection == "DM_EXPORT" || cursection == "DM_LOCAL_SERVICES") {
        auto &e = cfg.exports.emplace_back();
        e.servicename = object;
        e.laccesspoint = get(kv, "LACCESSPOINT", "");
        take_extra(kv, &e.extra, {"LACCESSPOINT"});
      } else if (cursection == "DM_IMPORT" || cursection == "DM_REMOTE_SERVICES") {
        auto &e = cfg.imports.emplace_back();
        e.servicename = object;
        e.laccesspoint = get(kv, "LACCESSPOINT", "");
        e.raccesspoint = get(kv, "RACCESSPOINT", get(kv, "RDOM", ""));
        e.rname = get(kv, "RNAME", object);
        take_extra(kv, &e.extra, {"LACCESSPOINT", "RACCESSPOINT", "RDOM", "RNAME"});
      } else {
        throw basic_parser_error("unknown DM section", row_, cursection);
      }
    }
    return cfg;
  }

 private:
  using params = std::map<std::string, std::string>;

  static std::string get(const params &kv, const std::string &key,
                         const std::string &def) {
    auto it = kv.find(key);
    return it == kv.end() ? def : it->second;
  }

  static void take_extra(const params &kv, params *extra,
                         std::initializer_list<const char *> known) {
    for (auto &p : kv) {
      bool is_known = false;
      for (auto k : known) {
        if (p.first == k) {
          is_known = true;
          break;
        }
      }
      if (!is_known) {
        (*extra)[p.first] = p.second;
      }
    }
  }
};

std::string emit_params(const std::map<std::string, std::string> &extra) {
  std::string out;
  for (auto &kv : extra) {
    out += " " + kv.first + "=\"" + kv.second + "\"";
  }
  return out;
}

}  // namespace

dmconfig dm_parse(std::istream &in) {
  dmreader reader(in);
  return reader.parse();
}

dmconfig dm_load_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  struct {
    char magic[8];
    uint32_t hdr_size;
    uint32_t version;
    uint64_t text_size;
  } hdr = {};
  in.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
  if (memcmp(hdr.magic, "FUXBDM01", 8) != 0 || hdr.hdr_size != sizeof(hdr)) {
    throw std::runtime_error("not a fuxedo BDMCONFIG file");
  }
  std::string text(hdr.text_size, '\0');
  in.read(text.data(), hdr.text_size);
  if (static_cast<uint64_t>(in.gcount()) != hdr.text_size) {
    throw std::runtime_error("truncated BDMCONFIG file");
  }
  std::istringstream sin(text);
  return dm_parse(sin);
}

std::string dm_emit(const dmconfig &cfg) {
  std::ostringstream out;
  out << "# DMCONFIG FILE\n\n\n*DM_RESOURCES\n\n\n\n";
  if (!cfg.locals.empty()) {
    out << "*DM_LOCAL\n\n";
    for (auto &e : cfg.locals) {
      out << "\"" << e.accesspoint << "\"\tGWGRP=\"" << e.gwgrp << "\"\n"
          << "\tACCESSPOINTID=\"" << e.accesspointid << "\"\n"
          << "\tTYPE=" << e.type << "\n"
          << "\tBLOCKTIME=" << e.blocktime << "\n"
          << "\tDMTLOGNAME=\"" << e.dmtlogname << "\"\n"
          << "\tMAXTRAN=" << e.maxtran << "\n"
          << "\tCONNECTION_POLICY=" << e.connection_policy
          << emit_params(e.extra) << "\n\n";
    }
  }
  if (!cfg.remotes.empty()) {
    out << "*DM_REMOTE\n\n";
  }
  for (auto &e : cfg.remotes) {
    out << "\"" << e.accesspoint << "\"\tACCESSPOINTID=\""
        << e.accesspointid << "\"\n\tTYPE=" << e.type
        << emit_params(e.extra) << "\n\n";
  }
  if (!cfg.tdomain.empty()) {
    out << "*DM_TDOMAIN\n\n";
  }
  for (auto &e : cfg.tdomain) {
    out << "\"" << e.accesspoint << "\"\tNWADDR=\"" << e.nwaddr << "\""
        << (e.connection_policy.empty()
                ? ""
                : " CONNECTION_POLICY=" + e.connection_policy)
        << emit_params(e.extra) << "\n\n";
  }
  if (!cfg.exports.empty()) {
    out << "*DM_EXPORT\n\n";
  }
  for (auto &e : cfg.exports) {
    out << "\"" << e.servicename << "\"\tLACCESSPOINT=\"" << e.laccesspoint
        << "\"" << emit_params(e.extra) << "\n\n";
  }
  if (!cfg.imports.empty()) {
    out << "*DM_IMPORT\n\n";
  }
  for (auto &e : cfg.imports) {
    out << "\"" << e.servicename << "\"\tLACCESSPOINT=\"" << e.laccesspoint
        << "\"\n\tRACCESSPOINT=\"" << e.raccesspoint << "\""
        << (e.rname.empty() || e.rname == e.servicename
                ? ""
                : "\n\tRNAME=\"" + e.rname + "\"")
        << emit_params(e.extra) << "\n\n";
  }
  return out.str();
}

}  // namespace fux::cfg
