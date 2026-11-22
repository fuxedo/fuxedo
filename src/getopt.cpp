// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// POSIX-style getopt(3), the flavor the platform's server library
// interposes (pinned on Oracle 12.2.2: their libengine exports getopt
// with these semantics). Differences from glibc's default:
//   - stops at the first non-option argument (no permutation)
//   - "--" terminates the scan but stays in argv
// Classic services (bankapp's TLR/ACCTMGR et al) rely on this: their
// CLOPT puts framework options and "--" first, application options
// after, and their getopt loops must see the application options.

#include <stdio.h>
#include <string.h>

extern "C" {
extern int opterr;
extern int optind;
extern char *optarg;
extern int optopt;

int getopt(int argc, char *const argv[], const char *optstring) {
  static const char *nextchar = nullptr;

  optarg = nullptr;
  if (nextchar == nullptr || *nextchar == '\0') {
    if (optind >= argc) {
      return -1;
    }
    const char *arg = argv[optind];
    if (arg[0] != '-' || arg[1] == '\0') {
      // a non-option argument ends the scan, no permutation
      return -1;
    }
    if (arg[1] == '-' && arg[2] == '\0') {
      // "--" ends the scan, is left in place, and the next index
      // starts the application argument region
      optind++;
      return -1;
    }
    nextchar = arg + 1;
  }

  int ch = *nextchar++;
  optopt = ch;
  const char *spec = strchr(optstring, ch);
  bool last_in_cluster = *nextchar == '\0';

  if (spec == nullptr) {
    if (opterr != 0) {
      fprintf(stderr, "%s: illegal option -- %c\n", argv[0], ch);
    }
  } else if (spec[1] == ':') {
    if (!last_in_cluster) {
      // attached value: -T100 or cluster tail
      optarg = const_cast<char *>(nextchar);
      nextchar = nullptr;
      optind++;
    } else if (spec[2] == ':') {
      // optional values must be attached
      nextchar = nullptr;
      optind++;
    } else if (optind + 1 < argc) {
      optarg = argv[optind + 1];
      nextchar = nullptr;
      optind += 2;
    } else {
      // missing required argument
      if (opterr != 0) {
        fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0],
                ch);
      }
      nextchar = nullptr;
      optind++;
      return optstring[0] == ':' ? ':' : '?';
    }
    return ch;
  }

  if (last_in_cluster) {
    nextchar = nullptr;
    optind++;
  }
  return spec == nullptr ? '?' : ch;
}
}  // extern "C"
