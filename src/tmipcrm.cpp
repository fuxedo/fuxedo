// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

#include "mib.h"

static std::string input(const std::string &prompt) {
  std::cout << prompt << std::flush;
  std::string in;
  std::getline(std::cin, in);
  return in;
}

int main(int argc, char *argv[]) {
  bool show_help = false;
  bool yes = false;

  auto parser = clara::Help(show_help) |
                clara::Opt(yes)["-y"]("answer Yes to all questions");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  /*
     The following IPC recources were found:

  Message Queues:
        5701633
        5734402

  Shared Memory:
        28770304

  Semaphores:
        1835009
        1769472

Remove IPC resources (y/n)?:
*/

  try {
    auto cfg = getconfig();

    mib m(cfg);

    if (yes) {
      m.remove();
    } else {
      while (true) {
        std::string in = input("Remove IPC resources (y/n)?: ");
        if (in.size() == 1) {
          if (in[0] == 'y') {
            m.remove();
            break;
          } else if (in[0] == 'n') {
            break;
          }
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
