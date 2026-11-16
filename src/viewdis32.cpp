// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "view32.h"

static const char *type_name(int type) {
  switch (type) {
    case FLD_SHORT:
      return "short";
    case FLD_LONG:
      return "long";
    case FLD_CHAR:
      return "char";
    case FLD_FLOAT:
      return "float";
    case FLD_DOUBLE:
      return "double";
    case FLD_STRING:
      return "string";
    case FLD_CARRAY:
      return "carray";
    default:
      return "long";
  }
}

static std::string null_value(const fux::view::member &m) {
  if (m.null_none) {
    return "NONE";
  }
  if (m.type == FLD_STRING || m.type == FLD_CARRAY) {
    if (m.nullval.empty()) {
      return "\"\"";
    }
    std::string out = "\"";
    for (char c : m.nullval) {
      if (c == '"' || c == '\\') {
        out += '\\';
      }
      out += c;
    }
    out += '"';
    return out;
  }
  if (m.type == FLD_CHAR) {
    char c = m.nullval.empty() ? 0 : m.nullval[0];
    if (c == 0) {
      return "\\0";
    }
    return "'" + std::string(1, c) + "'";
  }
  if (m.type == FLD_FLOAT || m.type == FLD_DOUBLE) {
    char buf[64];
    if (m.type == FLD_FLOAT) {
      float f = 0;
      if (!m.nullval.empty()) {
        std::memcpy(&f, m.nullval.data(),
                    std::min(m.nullval.size(), sizeof(f)));
      }
      snprintf(buf, sizeof(buf), "%f", static_cast<double>(f));
    } else {
      double d = 0;
      if (!m.nullval.empty()) {
        std::memcpy(&d, m.nullval.data(),
                    std::min(m.nullval.size(), sizeof(d)));
      }
      snprintf(buf, sizeof(buf), "%f", d);
    }
    return buf;
  }
  long l = 0;
  if (!m.nullval.empty()) {
    std::memcpy(&l, m.nullval.data(), std::min(m.nullval.size(), sizeof(l)));
  }
  return std::to_string(l);
}

static std::string flags(const fux::view::member &m) {
  std::string out;
  if (m.flags & F_COUNT) {
    out += "c,";
  }
  if (m.flags & F_LENGTH) {
    out += "l,";
  }
  if (m.flags & F_PROP) {
    out += "p,";
  }
  if (out.empty()) {
    if ((m.flags & (F_STOF | F_FTOS)) == (F_STOF | F_FTOS)) {
      out = "-";
    } else if (m.flags & F_STOF) {
      out = "f";
    } else if (m.flags & F_FTOS) {
      out = "s";
    } else {
      out = "n";
    }
  } else {
    out.pop_back();
  }
  return out;
}

static int disassemble(const std::string &file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    std::cerr << file << ": can not open file" << std::endl;
    return 1;
  }
  std::vector<fux::view::viewdef> views;
  try {
    views = fux::view::read_file(in);
  } catch (const std::exception &e) {
    std::cerr << file << ": " << e.what() << std::endl;
    return 1;
  }

  std::cout << "# VIEW OBJFILE = " << file << " " << std::endl << std::endl;
  for (auto &v : views) {
    std::cout << "VIEW   " << v.name << " " << std::endl << std::endl;
    std::cout << "# TYPE   CNAME        FBNAME     COUNT   FLAG  SIZE  NULL VALUE"
              << std::endl;
    std::cout << "# -----  --------     ---------  -----   ----  ----  ----------"
              << std::endl
              << std::endl;
    for (auto &m : v.members) {
      printf("%-8s %-12s %-10s %5d \t %s\t%d    \t%s\n", type_name(m.type),
             m.cname.c_str(), m.fbname.empty() ? "-" : m.fbname.c_str(),
             m.count, flags(m).c_str(), m.elsize, null_value(m).c_str());
      if (m.alm_off != -1) {
        printf("#\t\t\t\t\t L_OFFSET   = %d\n", m.alm_off);
      }
      if (m.acm_off != -1) {
        printf("#\t\t\t\t\t C_OFFSET   = %d\n", m.acm_off);
      }
      printf("#\t\t\t\t\tDATA OFFSET = %d\n\n", m.off);
    }
    std::cout << "END" << std::endl << std::endl;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: viewdis32 viewobjfile [viewobjfile ...]"
              << std::endl;
    return -1;
  }
  int rc = 0;
  for (int i = 1; i < argc; i++) {
    rc |= disassemble(argv[i]);
  }
  return rc;
}
