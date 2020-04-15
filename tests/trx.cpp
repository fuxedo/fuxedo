// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <stdexcept>

#include "../src/ipc.h"
#include "../src/trx.h"

TEST_CASE("transaction_table size limit", "[trx]") {
  auto n = transaction_table::needed(1, 50);
  auto *table = reinterpret_cast<transaction_table *>(calloc(n, 1));
  table->init(1, 50);

  REQUIRE(table->start(1) == 0);
  REQUIRE_THROWS_AS(table->start(2), std::out_of_range);

  free(table);
}

TEST_CASE("transaction_table transaction release", "[trx]") {
  auto n = transaction_table::needed(1, 50);
  auto *table = reinterpret_cast<transaction_table *>(calloc(n, 1));
  table->init(1, 50);

  REQUIRE(table->start(1) == 0);
  table->end(0);
  REQUIRE(table->start(2) == 0);

  free(table);
}

TEST_CASE("transaction_table bounds check", "[trx]") {
  auto n = transaction_table::needed(2, 2);
  auto *table = reinterpret_cast<transaction_table *>(calloc(n, 1));
  table->init(2, 2);

  (*table)[0];
  (*table)[1];
  REQUIRE_THROWS_AS((*table)[2], std::out_of_range);

  REQUIRE(!table->has_joined(0, 0));
  table->join(0, 0);
  REQUIRE(table->has_joined(0, 0));

  REQUIRE(!table->has_joined(0, 1));
  table->join(0, 1);
  REQUIRE(table->has_joined(0, 1));

  REQUIRE_THROWS_AS(table->join(0, 2), std::out_of_range);
  REQUIRE_THROWS_AS(table->has_joined(0, 3), std::out_of_range);

  free(table);
}

TEST_CASE("XID stringification", "[trx]") {
  std::string ethalon =
      "1279875137.70797228836149378b6fd6a315481425."
      "80e88a292250da8f8b2ee3a5959e8650";
  XID xid = fux::to_xid(ethalon);
  REQUIRE(xid.formatID == 1279875137);
  REQUIRE(fux::to_string(&xid) == ethalon);
}
