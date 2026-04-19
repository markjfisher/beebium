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

#include "beebium/econet/piconet/Events.hpp"

#include "beebium/econet/piconet/Base64.hpp"

#include <charconv>
#include <vector>

namespace beebium::piconet {

namespace {

// Split a line on single spaces, returning the tokens. Empty tokens (from
// adjacent spaces) are preserved. Caller passes the line without trailing
// terminator.
std::vector<std::string_view> split_on_space(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ' ') {
            tokens.emplace_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    tokens.emplace_back(line.substr(start));
    return tokens;
}

// Parse a non-negative integer in the given base. Returns true on success.
template <typename T>
bool parse_unsigned(std::string_view text, int base, T& out) {
    if (text.empty()) return false;
    T value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (ec != std::errc{} || ptr != text.data() + text.size()) return false;
    out = value;
    return true;
}

ParsedEvent unknown(std::string_view line) {
    return UnknownEvent{std::string(line)};
}

ParsedEvent parse_status(const std::vector<std::string_view>& tokens, std::string_view line) {
    // STATUS <maj.min.patch> <station_dec> <sr1_hex2> <mode_dec>
    if (tokens.size() != 5) return unknown(line);

    StatusEvent ev;

    // Version: dot-separated maj.min.patch.
    auto version = tokens[1];
    auto first_dot = version.find('.');
    if (first_dot == std::string_view::npos) return unknown(line);
    auto second_dot = version.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return unknown(line);
    if (!parse_unsigned(version.substr(0, first_dot), 10, ev.version_major)) return unknown(line);
    if (!parse_unsigned(version.substr(first_dot + 1, second_dot - first_dot - 1), 10, ev.version_minor)) return unknown(line);
    if (!parse_unsigned(version.substr(second_dot + 1), 10, ev.version_patch)) return unknown(line);

    // Station: decimal 0-255.
    std::uint32_t station = 0;
    if (!parse_unsigned(tokens[2], 10, station) || station > 0xFF) return unknown(line);
    ev.station = static_cast<std::uint8_t>(station);

    // SR1: hex 2 chars (the printf format is "%02x").
    std::uint32_t sr1 = 0;
    if (!parse_unsigned(tokens[3], 16, sr1) || sr1 > 0xFF) return unknown(line);
    ev.status_register_1 = static_cast<std::uint8_t>(sr1);

    // Mode: decimal 0/1/2.
    std::uint32_t mode_int = 0;
    if (!parse_unsigned(tokens[4], 10, mode_int)) return unknown(line);
    switch (mode_int) {
        case 0: ev.mode = Mode::Stop;    break;
        case 1: ev.mode = Mode::Listen;  break;
        case 2: ev.mode = Mode::Monitor; break;
        default: return unknown(line);
    }

    return ev;
}

ParsedEvent parse_two_b64_fields(const std::vector<std::string_view>& tokens,
                                  std::string_view line,
                                  bool is_immediate) {
    // RX_TRANSMIT or RX_IMMEDIATE: <tag> <scout_b64> <data_b64>
    if (tokens.size() != 3) return unknown(line);
    auto scout = decode_base64(tokens[1]);
    auto data  = decode_base64(tokens[2]);
    if (!scout || !data) return unknown(line);
    if (is_immediate) {
        return RxImmediateEvent{std::move(*scout), std::move(*data)};
    } else {
        return RxTransmitEvent{std::move(*scout), std::move(*data)};
    }
}

}  // namespace

ParsedEvent parse_event_line(std::string_view line) {
    if (line.empty()) return UnknownEvent{};

    auto tokens = split_on_space(line);
    if (tokens.empty()) return unknown(line);
    auto tag = tokens[0];

    if (tag == "STATUS") {
        return parse_status(tokens, line);
    }

    if (tag == "TX_RESULT") {
        if (tokens.size() != 2) return unknown(line);
        return TxResultEvent{parse_tx_result(tokens[1])};
    }

    if (tag == "REPLY_RESULT") {
        if (tokens.size() != 2) return unknown(line);
        return ReplyResultEvent{parse_tx_result(tokens[1])};
    }

    if (tag == "RX_TRANSMIT") {
        return parse_two_b64_fields(tokens, line, /*is_immediate=*/false);
    }

    if (tag == "RX_IMMEDIATE") {
        return parse_two_b64_fields(tokens, line, /*is_immediate=*/true);
    }

    if (tag == "RX_BROADCAST") {
        if (tokens.size() != 2) return unknown(line);
        auto data = decode_base64(tokens[1]);
        if (!data) return unknown(line);
        return RxBroadcastEvent{std::move(*data)};
    }

    if (tag == "MONITOR") {
        if (tokens.size() != 2) return unknown(line);
        auto data = decode_base64(tokens[1]);
        if (!data) return unknown(line);
        return MonitorEvent{std::move(*data)};
    }

    if (tag == "ERROR") {
        // Free-form ASCII remainder. Recover everything after "ERROR" and the
        // single space (or nothing if just "ERROR" alone).
        if (line.size() == 5) {
            return ErrorEvent{};
        }
        return ErrorEvent{std::string(line.substr(6))};
    }

    return unknown(line);
}

}  // namespace beebium::piconet
