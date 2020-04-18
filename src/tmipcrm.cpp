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

static void print(std::vector<int> &q, std::vector<int> &m,
                  std::vector<int> &s) {
  std::cout << "The following IPC resources were found:" << std::endl;
  std::cout << std::endl;
  std::cout << "Message Queues:" << std::endl;
  for (int i : q) {
    std::cout << "        " << i << std::endl;
  }
  std::cout << std::endl;
  std::cout << "Shared Memory:" << std::endl;
  for (int i : m) {
    std::cout << "        " << i << std::endl;
  }
  std::cout << std::endl;
  std::cout << "Semaphores:" << std::endl;
  for (int i : s) {
    std::cout << "        " << i << std::endl;
  }
  std::cout << std::endl;
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

  try {
    auto cfg = getconfig();

    mib mib(cfg);
    std::cout << "Looking for IPC resources in TUXCONFIG file "
              << std::getenv("TUXCONFIG") << std::endl;
    std::vector<int> q, m, s;
    mib.collect(q, m, s);

    if (q.empty() && m.empty() && s.empty()) {
      return 0;
    }

    print(q, m, s);

    if (yes) {
      mib.remove(q, m, s);
    } else {
      while (true) {
        std::string in = input("Remove IPC resources (y/n)?: ");
        if (in.size() == 1) {
          if (in[0] == 'y') {
            mib.remove(q, m, s);
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
