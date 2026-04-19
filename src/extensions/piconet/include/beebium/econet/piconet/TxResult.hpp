// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <string_view>

namespace beebium::piconet {

// Outcomes of TX/BCAST commands as reported by the firmware in TX_RESULT
// events. Names mirror the firmware's econet_tx_result_t enumeration values
// (the wire-format strings emitted by _tx_error_to_str).
//
// Source: piconet/board/src/econet.h lines 6-17 (enum definition);
//         piconet/board/src/piconet.c lines 532-557 (string mapping).
//
// Unknown is a host-side sentinel for codes that the firmware emits but our
// build does not recognise (e.g. a future firmware adds a new code). It is
// not a value the firmware emits.
enum class TxResult : std::uint8_t {
    Ok                 = 0,
    Uninitialised      = 1,
    Overflow           = 2,
    Underrun           = 3,
    LineJammed         = 4,
    NoScoutAck         = 5,
    NoDataAck          = 6,
    Timeout            = 7,
    InvalidReceiveId   = 8,
    Misc               = 9,
    Unknown            = 255,
};

// Parse a TX_RESULT event's code field. Returns TxResult::Unknown for codes
// not recognised by this build -- never throws, never fails. Whitespace is
// not stripped; pass an already-trimmed token.
TxResult parse_tx_result(std::string_view code);

// Convert a TxResult back to its wire-format string. Unknown maps to
// "UNKNOWN" (which is NOT a code the firmware emits -- it's a sentinel).
std::string_view to_string(TxResult result);

}  // namespace beebium::piconet
