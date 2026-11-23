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

std::string field_value(char *at) {
  auto id = *reinterpret_cast<FLDID *>(at);
  auto len = *reinterpret_cast<FLDLEN *>(at + sizeof(FLDID));
  auto *value = at + sizeof(FLDID) + sizeof(FLDLEN);
  std::ostringstream os;
  switch (Fldtype(id)) {
    case FLD_SHORT:
      os << *reinterpret_cast<short *>(value);
      break;
    case FLD_LONG:
      os << *reinterpret_cast<long *>(value);
      break;
    case FLD_CHAR:
      os << *value;
      break;
    case FLD_FLOAT:
      os << *reinterpret_cast<float *>(value);
      break;
    case FLD_DOUBLE:
      os << *reinterpret_cast<double *>(value);
      break;
    default:
      os.write(value, len);
      break;
  }
  return os.str();
}

std::string field_name(FLDID id) {
  auto id32 = Fmkfldid32(Fldtype(id), Fldno(id));
  auto *n = Fname32(id32);
  if (n != nullptr) {
    return n;
  }
  return "#" + std::to_string(Fldno(id));
}

void print_field(char *at) {
  std::cout << field_name(*reinterpret_cast<FLDID *>(at)) << '\t'
            << field_value(at) << '\n';
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
          kv item;
          item.name = field_name(id);
          item.value = field_value(at - sizeof(FLDID) - sizeof(FLDLEN));
          items.push_back(std::move(item));
          occ++;
        }
      }
    }
    std::cout << '\n';
    tpfree(reinterpret_cast<char *>(fb));
    tpfree(reply);
  };

  // the current buffer is sent on every blank line; an "n" line starts
  // a new buffer; input pending at EOF is dropped
  while (std::getline(std::cin, line)) {
    if (line.empty() || line == "\r") {
      process();
      continue;
    }
    if (line == "n") {
      items.clear();
      continue;
    }
    if (line[0] == '#') {
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
  tpterm();
  return failures == 0 ? 0 : 1;
}
