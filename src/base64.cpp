// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <inttypes.h>
#include <string.h>
#include <stdexcept>

size_t base64encode(const void *ibuf, size_t ilen, char *obuf, size_t olen) {
  static const char encoding[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  auto blocks = ilen / 3;
  auto padded = ilen % 3;
  if (olen < (blocks * 4 + (padded > 0 ? 4 : 0))) {
    throw std::range_error("Not enough space for " + std::to_string(ilen) +
                           " bytes");
  }

  auto *bytes = reinterpret_cast<const uint8_t *>(ibuf);
  size_t ipos = 0, opos = 0;

  for (size_t i = 0; i < blocks; i++) {
    auto n = ((uint32_t)bytes[ipos++]) << 16;
    n += ((uint32_t)bytes[ipos++]) << 8;
    n += bytes[ipos++];

    obuf[opos++] = encoding[(n >> 18) & 63];
    obuf[opos++] = encoding[(n >> 12) & 63];
    obuf[opos++] = encoding[(n >> 6) & 63];
    obuf[opos++] = encoding[n & 63];
  }
  if (padded > 0) {
    auto n = ((uint32_t)bytes[ipos]) << 16;
    if (ipos + 1 < ilen) {
      n += ((uint32_t)bytes[ipos + 1]) << 8;
    }

    obuf[opos++] = encoding[(n >> 18) & 63];
    obuf[opos++] = encoding[(n >> 12) & 63];
    if (ipos + 1 < ilen) {
      obuf[opos++] = encoding[(n >> 6) & 63];
      obuf[opos++] = '=';
    } else {
      obuf[opos++] = '=';
      obuf[opos++] = '=';
    }
  }

  return opos;
}

size_t base64decode(const char *ibuf, size_t ilen, void *obuf, size_t olen) {
  static const uint8_t decoding[] = {
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 62,  128,
      128, 128, 63,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  128, 128,
      128, 128, 128, 128, 128, 0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
      10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
      25,  128, 128, 128, 128, 128, 128, 26,  27,  28,  29,  30,  31,  32,  33,
      34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
      49,  50,  51,  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
      128};

  auto blocks = ilen / 4;
  if (olen < blocks * 3) {
    throw std::range_error("Not enough space for " + std::to_string(ilen) +
                           " chars");
  }
  if (ilen % 4) {
    throw std::logic_error("Invalid input length or no padding");
  }

  const auto *ibytes = reinterpret_cast<const uint8_t *>(ibuf);
  auto *obytes = reinterpret_cast<uint8_t *>(obuf);
  size_t ipos = 0, opos = 0;
  uint8_t c0, c1, c2, c3, err = 0;

  // All blocks except the last one
  for (size_t i = 1; i < blocks; i++) {
    err |= c0 = decoding[ibytes[ipos++]];
    err |= c1 = decoding[ibytes[ipos++]];
    err |= c2 = decoding[ibytes[ipos++]];
    err |= c3 = decoding[ibytes[ipos++]];

    uint32_t n = (c0 << 18 | c1 << 12 | c2 << 6 | c3);
    obytes[opos++] = (n >> 16) & 255;
    obytes[opos++] = (n >> 8) & 255;
    obytes[opos++] = n & 255;
  }

  if (ipos < ilen) {
    auto outc = 1;
    err |= c0 = decoding[ibytes[ipos++]];
    err |= c1 = decoding[ibytes[ipos++]];
    c2 = c3 = 0;
    if (ibytes[ipos] != '=') {
      outc++;
      err |= c2 = decoding[ibytes[ipos++]];
      if (ibytes[ipos] != '=') {
        outc++;
        err |= c3 = decoding[ibytes[ipos++]];
      }
    }

    uint32_t n = (c0 << 18 | c1 << 12 | c2 << 6 | c3);
    obytes[opos++] = (n >> 16) & 255;
    if (outc > 1) {
      obytes[opos++] = (n >> 8) & 255;
      if (outc > 2) {
        obytes[opos++] = n & 255;
      }
    }
  }

  if (err & 128) {
    throw std::invalid_argument("Not a valid base64 encoding");
  }
  return opos;
}
