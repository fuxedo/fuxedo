#pragma once
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

#include "basic_parser.h"
#include "ubb.h"

class ubbreader : public basic_parser {
 public:
  ubbreader(std::istream &f) : basic_parser(f) {}
  ubbreader(FILE *f) : basic_parser(f) {}

  ubbconfig parse() {
    ubbconfig config;

    std::string tmp, section;
    std::string object;

    while (true) {
      ubb_line ubbline;

      if (parse_till_eol()) {
      } else if (parse_section(&tmp)) {
        section = tmp;
        tmp.clear();
      } else if (parameter(&object)) {
        whitespace();
        if (section == "RESOURCES") {
          std::string value;
          if (!parameter(&value)) {
            throw basic_parser_error("RESOURCES section must have PARAM VALUE",
                                     row_, "VALUE");
          }
          whitespace();
          if (!accept('\n')) {
            throw basic_parser_error("RESOURCES section must have PARAM VALUE",
                                     row_, "");
          }
          config.resources[object] = value;
        } else {
          ubb_params ubbparams;
          parse_params(&ubbparams);
          if (section == "SERVERS") {
            config.servers.push_back(std::make_pair(object, ubbparams));
          } else if (section == "GROUPS") {
            config.groups.push_back(std::make_pair(object, ubbparams));
          } else if (section == "MACHINES") {
            config.machines.push_back(std::make_pair(object, ubbparams));
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
  bool parse_till_eol() {
    whitespace();
    if (accept('#')) {
      while (accept([](int c) { return c != '\n' && c != EOF; }))
        ;
    }
    return accept('\n');
  }

  bool parse_section(std::string *section) {
    if (accept('*')) {
      if (!name(section)) {
        throw basic_parser_error("section name expected", row_, "*SECTION");
      }
      if (!parse_till_eol()) {
        throw basic_parser_error("newline expected", row_, "*SECTION");
      }
      return true;
    }
    return false;
  }

  bool parse_params(ubb_params *ubbparams) {
    while (true) {
      whitespace();
      // Continued on next line
      if (accept('\n')) {
        if (!whitespace()) {
          return true;
        }
      }

      std::string key, value;
      if (!parameter(&key)) {
        throw basic_parser_error("key=value expected", row_, "KEY");
      }
      whitespace();
      if (!accept('=')) {
        throw basic_parser_error("key=value expected", row_, "=");
      }
      whitespace();
      if (!parameter(&value)) {
        throw basic_parser_error("key=value expected", row_, "VALUE");
      }
      ubbparams->insert(std::make_pair(key, value));
    }
    return false;
  }

  bool whitespace() {
    if (accept([](int c) { return ::isspace(c) && c != '\n'; })) {
      while (accept([](int c) { return ::isspace(c) && c != '\n'; }))
        ;
      return true;
    }
    return false;
  }

  bool parameter(std::string *s = nullptr) {
    if (accept('"')) {
      while (accept([](int c) { return c != '"' && c != EOF; }, s))
        ;
      if (!accept('"')) {
        throw basic_parser_error("closing quote expected", row_, "");
      }
      return true;
    } else if (accept(
                   [](int c) { return !::isspace(c) && c != '#' && c != EOF; },
                   s)) {
      while (accept(
          [](int c) {
            return !::isspace(c) && c != '#' && c != '=' && c != EOF;
          },
          s))
        ;
      return true;
    }
    return false;
  }
};
