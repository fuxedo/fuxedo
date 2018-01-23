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

#include <catch.hpp>
#include <stdexcept>

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
