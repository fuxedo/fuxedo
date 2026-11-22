// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// 16-bit ud(1): reads an FML16 buffer in Fprint text form from stdin,
// calls the service named by the SRVCNM field (or -s), and prints the
// reply the same way. The classic samples (bankapp populate) drive
// their services through this tool.

#include <atmi.h>
#include <fml.h>
#include <fml32.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct kv {
  std::string name;
  std::string value;
};

void add(FBFR *fb, const kv &item) {
  auto id = Fldid(const_cast<char *>(item.name.c_str()));
  if (id == BADFLDID) {
    return;
  }
  const char *v = item.value.c_str();
  switch (Fldtype(id)) {
    case FLD_SHORT: {
      short s = static_cast<short>(atol(v));
      Fadd(fb, id, reinterpret_cast<char *>(&s), 0);
      break;
    }
    case FLD_LONG: {
      long l = atol(v);
      Fadd(fb, id, reinterpret_cast<char *>(&l), 0);
      break;
    }
    case FLD_CHAR: {
      char c = v[0];
      Fadd(fb, id, &c, 0);
      break;
    }
    case FLD_FLOAT: {
      float f = static_cast<float>(atof(v));
      Fadd(fb, id, reinterpret_cast<char *>(&f), 0);
      break;
    }
    case FLD_DOUBLE: {
      double d = atof(v);
      Fadd(fb, id, reinterpret_cast<char *>(&d), 0);
      break;
    }
    case FLD_CARRAY:
      Fadd(fb, id, const_cast<char *>(v), static_cast<FLDLEN>(item.value.size()));
      break;
    default:
      Fadd(fb, id, const_cast<char *>(v), 0);
      break;
  }
}

void print_field(char *at) {
  auto id = *reinterpret_cast<FLDID *>(at);
  auto len = *reinterpret_cast<FLDLEN *>(at + sizeof(FLDID));
  auto *value = at + sizeof(FLDID) + sizeof(FLDLEN);
  std::string name;
  auto id32 = Fmkfldid32(Fldtype(id), Fldno(id));
  auto *n = Fname32(id32);
  if (n != nullptr) {
    name = n;
  } else {
    name = "#" + std::to_string(Fldno(id));
  }
  std::cout << name << '\t';
  switch (Fldtype(id)) {
    case FLD_SHORT:
      std::cout << *reinterpret_cast<short *>(value);
      break;
    case FLD_LONG:
      std::cout << *reinterpret_cast<long *>(value);
      break;
    case FLD_CHAR:
      std::cout << *value;
      break;
    case FLD_FLOAT:
      std::cout << *reinterpret_cast<float *>(value);
      break;
    case FLD_DOUBLE:
      std::cout << *reinterpret_cast<double *>(value);
      break;
    default:
      std::cout.write(value, len);
      break;
  }
  std::cout << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const char *svc = nullptr;
  long timeout = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      svc = argv[++i];
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      timeout = atol(argv[++i]);
    }
  }
  if (tpinit(nullptr) == -1) {
    std::cerr << "tpinit failed " << tperrno << '\n';
    return 1;
  }
  if (timeout > 0) {
    tpsblktime(static_cast<int>(timeout), TPBLK_ALL);
  }

  int failures = 0;
  std::vector<kv> items;
  std::string line;
  auto process = [&]() {
    if (items.empty()) {
      return;
    }
    auto *fb = reinterpret_cast<FBFR *>(
        tpalloc(const_cast<char *>("FML"), nullptr, 8192));
    if (fb == nullptr) {
      std::cerr << "tpalloc FML failed " << tperrno << '\n';
      failures++;
      items.clear();
      return;
    }
    for (auto &item : items) {
      add(fb, item);
    }
    items.clear();

    const char *target = svc;
    std::string srvnm;
    if (target == nullptr) {
      auto *v = Fvals(fb, Fldid(const_cast<char *>("SRVCNM")), 0);
      if (v == nullptr) {
        std::cerr << "no SRVCNM field and no -s\n";
        failures++;
        tpfree(reinterpret_cast<char *>(fb));
        return;
      }
      srvnm = v;
      target = srvnm.c_str();
    }

    char *reply = tpalloc(const_cast<char *>("FML"), nullptr, 8192);
    long rlen = 8192;
    bool ok = tpcall(const_cast<char *>(target),
                     reinterpret_cast<char *>(fb), 0, &reply, &rlen, 0) != -1;
    if (!ok && tperrno != TPESVCFAIL) {
      std::cerr << "tpcall " << target << " failed " << tperrno << '\n';
      failures++;
      tpfree(reinterpret_cast<char *>(fb));
      tpfree(reply);
      return;
    }
    // print the reply buffer the same text way: walk it via Ffind
    // over id numbers and occurrences
    auto *rfb = reinterpret_cast<FBFR *>(reply);
    for (long no = 1; no < 8192; no++) {
      for (int type = 0; type <= FLD_CARRAY; type++) {
        auto id = Fmkfldid(type, no);
        FLDOCC occ = 0;
        while (Ffind(rfb, id, occ, nullptr) != nullptr) {
          FLDLEN len = 0;
          auto *at = Ffind(rfb, id, occ, &len);
          // the record header sits right before the value
          print_field(at - sizeof(FLDID) - sizeof(FLDLEN));
          occ++;
        }
      }
    }
    std::cout << '\n';
    tpfree(reinterpret_cast<char *>(fb));
    tpfree(reply);
  };

  // Fprint text streams separate buffers with a blank line; the "n"
  // line is a buffer-start marker the tools emit
  while (std::getline(std::cin, line)) {
    if (line.empty() || line == "\r") {
      process();
      continue;
    }
    if (line == "n" || line[0] == '#') {
      continue;
    }
    auto sep = line.find_first_of("\t ");
    if (sep == std::string::npos) {
      continue;
    }
    auto end = line.find_last_not_of(" \t\r");
    kv item;
    item.name = line.substr(0, sep);
    item.value = line.substr(sep + 1, end - sep);
    items.push_back(std::move(item));
  }
  process();
  tpterm();
  return failures == 0 ? 0 : 1;
}
