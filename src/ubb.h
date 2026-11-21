#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

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
  ubb_section routing;

  ubbconfig()
      : resources{
            {"MAXACCESSERS", "50"},
            {"MAXSERVERS", "50"},
            {"MAXSERVICES", "100"},

        } {}
};

// "Binary" version of ubbconfig: fixed header followed by the text
// version.
struct tuxconfig {
  char magic[8];  // "FUXCFG01"
  uint32_t hdr_size;
  uint32_t version;  // 1
  uint32_t text_off;
  uint64_t text_size;
  uint32_t ipckey;
  uint32_t flags;  // bit0: populated by tmloadcf
  uint64_t revision;
};

constexpr char TUXCONFIG_MAGIC[8] = {'F', 'U', 'X', 'C', 'F', 'G', '0', '1'};
constexpr uint32_t TUXCONFIG_VERSION = 1;
constexpr uint32_t TUXCONFIG_FLAG_LOADED = 0x1;
