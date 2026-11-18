// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <clara.hpp>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "tlog.h"

int main(int argc, char *argv[]) {
  bool show_help = false;
  uint64_t offset = 0;
  uint64_t size = 100;
  std::string device;

  auto parser =
      clara::Help(show_help) |
      clara::Opt(offset, "offset")["-o"]("first log page (an emulated offset)") |
      clara::Opt(size, "size")["-s"]("log size in pages") |
      clara::Arg(device, "device")("the TLOG device (a folder)");

  auto result = parser.parse(clara::Args(argc, argv));
  if (!result) {
    std::cerr << parser;
    return -1;
  }
  if (show_help) {
    std::cout << parser;
    return 1;
  }
  if (device.empty()) {
    std::cerr << "crdl: device is required" << std::endl;
    return 1;
  }
  if (size == 0) {
    std::cerr << "crdl: size must be positive" << std::endl;
    return 1;
  }

  try {
    fux::tlog::log::create(device, offset, size);
    std::cout << "Created transaction log " << device << " (offset "
              << offset << ", " << size << " pages)" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "crdl: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
