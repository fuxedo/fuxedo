// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <iostream>
#include <string>

#include "tlog.h"

int main(int argc, char *argv[]) {
  bool show_help = false;
  std::string device;

  auto parser = clara::Help(show_help) |
                clara::Arg(device, "device")("the TLOG device (a folder)");
  auto result = parser.parse(clara::Args(argc, argv));
  if (!result || show_help) {
    std::cerr << parser;
    return !result ? -1 : 1;
  }
  if (device.empty()) {
    std::cerr << "crlog: device is required" << std::endl;
    return 1;
  }

  try {
    auto log = fux::tlog::log::open(device);
    std::cout << "Transaction log " << device << " (offset " << log.offset()
              << ", " << log.size() << " pages)" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "crlog: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
