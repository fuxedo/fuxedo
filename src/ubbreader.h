#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "basic_parser.h"
#include "ubb.h"

class ubbreader : public basic_parser {
 public:
  ubbreader(std::istream &f) : basic_parser(f) {}
  ubbreader(FILE *f) : basic_parser(f) {}

  ubbconfig parse() {
    ubbconfig config;

    std::string tmp, cursection;
    std::string object;

    while (true) {
      if (skip_line()) {
      } else if (section(&tmp)) {
        cursection = tmp;
        tmp.clear();
      } else if (token(&object, false)) {
        blank();
        if (object == "DEFAULT:" || object == "DEFAULT") {
          // Default parameters for the entries of this section
          ubb_params defparams;
          kv_list(&defparams, true, true);
          auto &defs = defaults_[cursection];
          for (auto &kv : defparams) {
            defs[kv.first] = kv.second;
          }
        } else if (cursection == "RESOURCES") {
          if (sym_ == '=') {
            // Oracle grammar: RESOURCES is PARAM VALUE only, never
            // PARAM=VALUE; accepting it would silently parse a bogus
            // value starting with '='.
            throw basic_parser_error(
                "RESOURCES section must have PARAM VALUE", row_, "VALUE");
          }
          std::string value;
          if (!token(&value)) {
            throw basic_parser_error("RESOURCES section must have PARAM VALUE",
                                     row_, "VALUE");
          }
          blank();
          if (!accept('\n')) {
            throw basic_parser_error("RESOURCES section must have PARAM VALUE",
                                     row_, "");
          }
          config.resources[object] = value;
        } else {
          ubb_params ubbparams;
          kv_list(&ubbparams, true, true);
          // Section defaults apply unless the entry overrides them
          ubb_params merged(defaults_[cursection]);
          for (auto &kv : ubbparams) {
            merged[kv.first] = kv.second;
          }
          if (cursection == "SERVERS") {
            config.servers.push_back(std::make_pair(object, merged));
          } else if (cursection == "GROUPS") {
            config.groups.push_back(std::make_pair(object, merged));
          } else if (cursection == "MACHINES") {
            config.machines.push_back(std::make_pair(object, merged));
          } else if (cursection == "SERVICES") {
            config.services.push_back(std::make_pair(object, merged));
          }
        }
        object.clear();
      } else {
        break;
      }
    }

    if (sym_ != EOF) {
      throw basic_parser_error("unrecognized input", row_, "");
    }

    return config;
  }

 private:
  std::map<std::string, ubb_params> defaults_;
};
