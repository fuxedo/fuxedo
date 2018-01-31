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

#include <clara.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "fieldtbl32.h"

static void process_file(const std::string &file,
                         const std::string &output_directory) {
  std::ifstream fin;
  fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fin.open(file);

  field_table_parser p(fin);
  p.parse();

  std::map<int, std::string> field_type_names;
  for (auto &f : field_types) {
    field_type_names.insert(std::make_pair(f.second, f.first));
  }

  std::ofstream fout;
  auto slash = file.find_last_of('/');
  if (slash == std::string::npos) {
    slash = 0;
  }
  fout.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  fout.open(output_directory + "/" + file.substr(slash) + ".h");
  for (auto &i : p.entries()) {
    if (i.c && !i.c->empty()) {
      fout << "// " << *(i.c) << std::endl;
    } else if (i.r) {
      fout << *(i.r) << std::endl;
    } else if (i.f) {
      fout << "#define " << i.f->name << "\t((FLDID32)" << i.f->fieldid
           << ")\t\t// " << i.f->name << "\t" << Fldno32(i.f->fieldid) << "\t"
           << field_type_names[Fldtype32(i.f->fieldid)] << "\t" << i.f->comment
           << std::endl;
    }
  }
}

int main(int argc, char *argv[]) {
  bool show_help = false;

  std::string output_directory = ".";
  std::vector<std::string> files;

  auto parser =
      clara::Help(show_help) |
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
      process_file(file, output_directory);
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
