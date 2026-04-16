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

// Command formatters for the Piconet wire protocol. Pure functions producing
// the bytes to write to the serial port. Each result ends with '\r' (the
// firmware's command terminator).
//
// Source: piconet/board/src/piconet.c lines 421-505 (command parser).

#include "beebium/econet/piconet/Base64.hpp"
#include "beebium/econet/piconet/Mode.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace beebium::piconet {

inline std::string format_status() {
    return "STATUS\r";
}

inline std::string format_restart() {
    return "RESTART\r";
}

inline std::string format_set_mode(Mode mode) {
    switch (mode) {
        case Mode::Stop:    return "SET_MODE STOP\r";
        case Mode::Listen:  return "SET_MODE LISTEN\r";
        case Mode::Monitor: return "SET_MODE MONITOR\r";
    }
    return "SET_MODE STOP\r";  // Unreachable.
}

inline std::string format_set_station(std::uint8_t station) {
    // The firmware parses the station as a decimal integer
    // (piconet/board/src/piconet.c line 466: strtol(..., NULL, 10)).
    return "SET_STATION " + std::to_string(static_cast<unsigned>(station)) + "\r";
}

// Format a TX command. Field order matches the firmware parser at
// piconet/board/src/piconet.c lines 473-480:
//   TX <dest_stn> <dest_net> <ctrl_byte> <port> <data_b64> <scout_extra_b64>
// Numbers are decimal. The two base64 fields are: data FIRST, then optional
// scout-extra. If scout_extra is empty, the trailing field is omitted (the
// firmware handles a missing second token by treating scout_extra_len as 0).
inline std::string format_tx(std::uint8_t dest_stn,
                             std::uint8_t dest_net,
                             std::uint8_t control_byte,
                             std::uint8_t port,
                             std::span<const std::uint8_t> data,
                             std::span<const std::uint8_t> scout_extra = {}) {
    std::string out = "TX ";
    out += std::to_string(static_cast<unsigned>(dest_stn));
    out += ' ';
    out += std::to_string(static_cast<unsigned>(dest_net));
    out += ' ';
    out += std::to_string(static_cast<unsigned>(control_byte));
    out += ' ';
    out += std::to_string(static_cast<unsigned>(port));
    out += ' ';
    out += encode_base64(data);
    if (!scout_extra.empty()) {
        out += ' ';
        out += encode_base64(scout_extra);
    }
    out += '\r';
    return out;
}

// Format a BCAST command. The firmware stamps the source station from its
// own SET_STATION value and broadcasts to dest_stn=dest_net=0xFF; the
// command provides only the data payload.
// Source: piconet/board/src/piconet.c lines 481-483; econet.c lines 124-142.
inline std::string format_bcast(std::span<const std::uint8_t> data) {
    return "BCAST " + encode_base64(data) + "\r";
}

}  // namespace beebium::piconet
