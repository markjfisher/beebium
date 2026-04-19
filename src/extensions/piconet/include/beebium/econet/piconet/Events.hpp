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

// Event parser for the Piconet wire protocol. Parses a single line as
// emitted by the firmware (without the trailing '\n'). Malformed or
// unrecognised lines yield UnknownEvent rather than throwing -- a future
// firmware may emit codes we don't yet recognise, and PiconetBackend must
// log and continue rather than abort.
//
// Source: piconet/board/src/piconet.c lines 180-275 (event formatters).

#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/TxResult.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace beebium::piconet {

// STATUS <maj.min.patch> <station_dec> <sr1_hex2> <mode_dec>
// Source: piconet/board/src/piconet.c lines 189-196.
struct StatusEvent {
    std::uint32_t version_major = 0;
    std::uint32_t version_minor = 0;
    std::uint32_t version_patch = 0;
    std::uint8_t  station       = 0;
    std::uint8_t  status_register_1 = 0;
    Mode          mode          = Mode::Stop;
};

// TX_RESULT <code>
// Source: piconet/board/src/piconet.c lines 199-202.
struct TxResultEvent {
    TxResult result = TxResult::Unknown;
};

// REPLY_RESULT <code>  -- the host should never observe a useful value
// because the firmware's REPLY path is dead, but the line is still
// formatted by piconet.c lines 204-207. Parse defensively.
struct ReplyResultEvent {
    TxResult result = TxResult::Unknown;
};

// RX_TRANSMIT <scout_b64> <data_b64>
// Note: scout is the FIRST base64 field, data is the SECOND -- opposite
// to the TX command's field order.
// Source: piconet/board/src/piconet.c lines 246-256.
struct RxTransmitEvent {
    std::vector<std::uint8_t> scout;
    std::vector<std::uint8_t> data;
};

// RX_IMMEDIATE <scout_b64> <data_b64>
// Same field order as RX_TRANSMIT. Note that ctrl byte 0x88 (MachinePeek)
// never produces an RX_IMMEDIATE -- the firmware handles it inline.
// Source: piconet/board/src/piconet.c lines 234-244.
struct RxImmediateEvent {
    std::vector<std::uint8_t> scout;
    std::vector<std::uint8_t> data;
};

// RX_BROADCAST <data_b64>
// Source: piconet/board/src/piconet.c lines 228-232.
struct RxBroadcastEvent {
    std::vector<std::uint8_t> data;
};

// MONITOR <data_b64>  -- promiscuous frame capture, only emitted in
// MODE_MONITOR.
// Source: piconet/board/src/piconet.c lines 222-226.
struct MonitorEvent {
    std::vector<std::uint8_t> data;
};

// ERROR <free-form ASCII>  -- the message is unparsed (free-form).
// Source: piconet/board/src/piconet.c lines 210-211 (RX errors), 495 (parse errors).
struct ErrorEvent {
    std::string message;
};

// Anything we couldn't parse: malformed input, unknown event tag, wrong
// arity, bad base64 in a field, etc. Carries the raw line for logging.
struct UnknownEvent {
    std::string raw_line;
};

using ParsedEvent = std::variant<
    StatusEvent,
    TxResultEvent,
    ReplyResultEvent,
    RxTransmitEvent,
    RxImmediateEvent,
    RxBroadcastEvent,
    MonitorEvent,
    ErrorEvent,
    UnknownEvent>;

// Parse one complete event line (without the trailing '\n'). The input
// must be the body of one event -- split lines from the serial stream
// before calling.
ParsedEvent parse_event_line(std::string_view line);

}  // namespace beebium::piconet
