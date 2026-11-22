// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "view32.h"

static std::string basename(const std::string &file) {
  auto slash = file.find_last_of('/');
  auto start = slash == std::string::npos ? 0 : slash + 1;
  auto dot = file.find_last_of('.');
  if (dot == std::string::npos || dot < start) {
    dot = file.size();
  }
  return file.substr(start, dot - start);
}

static std::string quoted(const std::vector<char> &v) {
  std::string out;
  for (char c : v) {
    switch (c) {
      case '\0':
        out += "\\0";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\v':
        out += "\\v";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\'':
        out += "\\'";
        break;
      case '"':
        out += "\\\"";
        break;
      default:
        out += c;
    }
  }
  return out;
}

static std::string c_type(const fux::view::member &m) {
  switch (m.type) {
    case FLD_SHORT:
      return "short";
    case FLD_LONG:
      return "long";
    case FLD_CHAR:
    case FLD_STRING:
    case FLD_CARRAY:
      return "char";
    case FLD_FLOAT:
      return "float";
    case FLD_DOUBLE:
      return "double";
    default:
      return "long";
  }
}

static std::string null_comment(const fux::view::member &m) {
  char buf[64];
  if (m.null_none) {
    return "NONE";
  }
  if (m.type == FLD_STRING || m.type == FLD_CARRAY) {
    if (m.nullval.empty()) {
      return "\"\\0\"";
    }
    return "\"" + quoted(m.nullval) + "\"";
  }
  if (m.type == FLD_CHAR) {
    char c = m.nullval.empty() ? 0 : m.nullval[0];
    if (c == 0) {
      return "'\\0'";
    }
    snprintf(buf, sizeof(buf), "'%c'", c);
    return buf;
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
  snprintf(buf, sizeof(buf), "%ld", l);
  return buf;
}

static void write_header(std::ostream &out, const fux::view::viewdef &v) {
  out << "struct " << v.name << " {" << std::endl;
  for (auto &line : v.code) {
    out << line << std::endl;
  }
  for (auto &m : v.members) {
    if (m.flags & F_LENGTH) {
      out << "\tunsigned int\tL_" << m.cname;
      if (m.count > 1) {
        out << "[" << m.count << "]";
      }
      out << ";";
      if (m.count > 1) {
        out << "\t/* length array of " << m.cname << " */";
      } else {
        out << "\t/* length of " << m.cname << " */";
      }
      out << std::endl;
    }
    if (m.flags & F_COUNT) {
      out << "\tint\tC_" << m.cname << ";\t/* count of " << m.cname << " */"
          << std::endl;
    }
    out << "\t" << c_type(m) << "\t" << m.cname;
    if (m.count > 1) {
      out << "[" << m.count << "]";
    }
    if (m.type == FLD_STRING || m.type == FLD_CARRAY) {
      out << "[" << m.elsize << "]";
    }
    out << ";\t\t/* null=" << null_comment(m) << " */" << std::endl;
  }
  out << "};" << std::endl;
  out << std::endl;
}

static int process_file(const std::string &file, bool no_fml,
                        const std::string &output_directory) {
  std::ifstream fin(file);
  if (!fin) {
    std::cerr << file << ": can not open file" << std::endl;
    return 1;
  }

  std::vector<fux::view::viewdef> views;
  try {
    fux::view::viewfile_parser p(fin);
    views = p.parse();
  } catch (const basic_parser_error &e) {
    std::cerr << file << ": syntax error in line " << e.row << ": "
              << e.what();
    if (!e.extra.empty()) {
      std::cerr << " (" << e.extra << ")";
    }
    std::cerr << std::endl;
    return 1;
  }

  if (views.empty()) {
    std::cerr << file << ": no views defined" << std::endl;
    return 1;
  }

  std::vector<std::string> errors, warnings;
  bool ok = fux::view::resolve(views, no_fml, errors, warnings);
  for (auto &w : warnings) {
    std::cerr << file << ": warning: " << w << std::endl;
  }
  for (auto &e : errors) {
    std::cerr << file << ": error: " << e << std::endl;
  }
  if (!ok) {
    return 1;
  }

  for (auto &v : views) {
    fux::view::layout(v);
  }

  std::ofstream vout(output_directory + "/" + basename(file) + ".V",
                     std::ios::binary);
  if (!vout) {
    std::cerr << "can not create " << output_directory << "/" << basename(file)
              << ".V" << std::endl;
    return 1;
  }
  fux::view::write_file(vout, views);

  std::ofstream hout(basename(file) + ".h");
  if (!hout) {
    std::cerr << "can not create " << basename(file) << ".h" << std::endl;
    return 1;
  }
  for (auto &v : views) {
    write_header(hout, v);
  }
  return 0;
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool no_fml = false;
#ifdef VIEWC16_DEFAULTS
  no_fml = true;
#endif

  std::string output_directory = ".";
  std::vector<std::string> files;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(no_fml)["-n"]("compile an independent view") |
      clara::Opt(output_directory,
                 "output_directory")["-d"]("output directory for .V files") |
      clara::Arg(files, "viewfile")("viewfiles to compile").required();

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || result.value().type() != clara::ParseResultType::Matched ||
      show_help) {
    std::cerr << "viewc32 - compile viewfiles into .V files and C headers"
              << std::endl
              << parser;
    return -1;
  }

  int rc = 0;
  for (auto &file : files) {
    rc |= process_file(file, no_fml, output_directory);
  }
  return rc;
}
