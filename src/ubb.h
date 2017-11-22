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

#include <cstdint>
#include <map>
#include <vector>

typedef std::map<std::string, std::string> ubb_params;
typedef std::pair<std::string, ubb_params> ubb_line;
typedef std::vector<ubb_line> ubb_section;

struct ubbconfig {
  ubb_params resources;
  ubb_section machines;
  ubb_section groups;
  ubb_section servers;
  ubb_section services;

  ubbconfig()
      : resources{
            {"MAXACCESSERS", "50"},
            {"MAXSERVERS", "50"},
            {"MAXSERVICES", "100"},

        } {}
};

// "Binary" version of ubbconfig
// Really just some significant info at fixed offsets and then the text version
// ubbconfig
struct tuxconfig {
  size_t size;  // Size and "signature" at the same time
  uint32_t ipckey;
  uint16_t maxservers;
  uint16_t maxservices;
  uint16_t maxqueues;
  uint16_t maxgroups;
  uint16_t maxaccessers;
  //  char ubb[];
};
