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

#include <tx.h>

// S0 No RMs have been opened or initialised. An application thread of control
// cannot start a global transaction until it has successfully opened its RMs
// via tx_open().
// S1 The thread has opened its RMs but is not in a transaction. Its
// transaction_control characteristic is TX_UNCHAINED.
// S2 The thread has opened its RMs but is not in a transaction. Its
// transaction_control characteristic is TX_CHAINED.
// S3 The thread has opened its RMs and is in a transaction. Its
// transaction_control characteristic is TX_UNCHAINED.
// S4 The thread has opened its RMs and is in a transaction. Its
// transaction_control characteristic is TX_CHAINED.

enum class tx_state { s0 = 0, s1, s2, s3, s4 };

enum tx_state state = tx_state::s0;

int tx_begin() { return -1; }
int tx_close() { return -1; }
int tx_commit() { return -1; }
int tx_info(TXINFO *) { return -1; }
int tx_open() { return -1; }
int tx_rollback() { return -1; }
int tx_set_commit_return(COMMIT_RETURN) { return -1; }
int tx_set_transaction_control(TRANSACTION_CONTROL) { return -1; }
int tx_set_transaction_timeout(TRANSACTION_TIMEOUT) { return -1; }
