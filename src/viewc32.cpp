// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "view32.h"

static void process_file(const std::string &file, bool no_fml,
                         const std::string &output_directory) {
  std::ifstream fin;
  fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fin.open(file);

  view_parser p(fin);
  p.parse();

  std::ofstream fout;
  auto slash = file.find_last_of('/');
  if (slash == std::string::npos) {
    slash = 0;
  }
  auto dot = file.find_last_of('.');
  if (dot != std::string::npos && dot < slash) {
    dot = std::string::npos;
  }
  fout.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fout.open(output_directory + "/" + file.substr(slash, dot) + ".h");

  for (auto &i : p.entries()) {
    if (i.r) {
      fout << *(i.r) << std::endl;
    } else if (i.view) {
      fout << "struct " << *(i.view) << " {" << std::endl;
    } else if (i.end) {
      fout << "};" << std::endl;
    } else if (i.f) {
      if (i.f->flag.find("L") != std::string::npos) {
        fout << "\tunsigned int\tL_" << i.f->cname;
        if (i.f->count > 1) {
          fout << "[" << i.f->count << "]";
        }
        fout << ";" << std::endl;
       }
      if (i.f->flag.find("C") != std::string::npos) {
        fout << "\tint\tC_" << i.f->cname << ";" << std::endl;
       }

      fout << "\t";
      if (i.f->type == "carray" || i.f->type == "string") {
        fout << "char";
      } else {
        fout << i.f->type;
      }
      fout << "\t" << i.f->cname;
      if (i.f->count > 1) {
        fout << "[" << i.f->count << "]";
      }
      if (!i.f->size.empty()) {
        fout << "[" << i.f->size << "]";
      }
      fout << ";" << std::endl;
    }
  }
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool no_fml = false;

  std::string output_directory = ".";
  std::vector<std::string> files;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(no_fml)["-n"]("do not map to an FML buffer") |
      clara::Opt(output_directory,
                 "output_directory")["-d"]("output directory") |
      clara::Arg(files, "field_table")("field tables to process").required();

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || result.value().type() != clara::ParseResultType::Matched) {
    std::cerr << parser;
    return -1;
  }

  try {
    for (auto &file : files) {
      process_file(file, no_fml, output_directory);
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
