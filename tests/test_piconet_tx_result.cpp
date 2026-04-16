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

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/piconet/TxResult.hpp"

#include <array>

using namespace beebium::piconet;

TEST_CASE("TxResult: every firmware code parses to its enum value", "[piconet][protocol][txresult]") {
    // The list mirrors econet_tx_result_t in piconet/board/src/econet.h
    // and the strings emitted by _tx_error_to_str in piconet.c lines 532-557.
    struct Case {
        std::string_view code;
        TxResult         expected;
    };
    constexpr std::array<Case, 10> cases = {{
        {"OK",                 TxResult::Ok},
        {"UNINITIALISED",      TxResult::Uninitialised},
        {"OVERFLOW",           TxResult::Overflow},
        {"UNDERRUN",           TxResult::Underrun},
        {"LINE_JAMMED",        TxResult::LineJammed},
        {"NO_SCOUT_ACK",       TxResult::NoScoutAck},
        {"NO_DATA_ACK",        TxResult::NoDataAck},
        {"TIMEOUT",            TxResult::Timeout},
        {"INVALID_RECEIVE_ID", TxResult::InvalidReceiveId},
        {"MISC",               TxResult::Misc},
    }};

    for (const auto& c : cases) {
        CHECK(parse_tx_result(c.code) == c.expected);
    }
}

TEST_CASE("TxResult: unrecognised codes parse to Unknown (not a parse failure)",
          "[piconet][protocol][txresult]") {
    // Forward-compat: a future firmware may emit a new code. We must not fail
    // hard on it -- PiconetBackend should log and continue.
    CHECK(parse_tx_result("NOT_LISTENING") == TxResult::Unknown);  // Was in feasibility doc; not in firmware
    CHECK(parse_tx_result("FUTURE_CODE")   == TxResult::Unknown);
    CHECK(parse_tx_result("")              == TxResult::Unknown);
}

TEST_CASE("TxResult: parse is case-sensitive and not whitespace-tolerant",
          "[piconet][protocol][txresult]") {
    // The firmware emits exact tokens; we should not accidentally accept
    // mangled input. The parser receives an already-tokenised field.
    CHECK(parse_tx_result("ok")            == TxResult::Unknown);
    CHECK(parse_tx_result(" OK")           == TxResult::Unknown);
    CHECK(parse_tx_result("OK ")           == TxResult::Unknown);
    CHECK(parse_tx_result("OK\n")          == TxResult::Unknown);
}

TEST_CASE("TxResult: round-trip parse(to_string(x)) == x for every code",
          "[piconet][protocol][txresult]") {
    constexpr std::array<TxResult, 10> codes = {
        TxResult::Ok, TxResult::Uninitialised, TxResult::Overflow,
        TxResult::Underrun, TxResult::LineJammed, TxResult::NoScoutAck,
        TxResult::NoDataAck, TxResult::Timeout, TxResult::InvalidReceiveId,
        TxResult::Misc,
    };
    for (auto c : codes) {
        CHECK(parse_tx_result(to_string(c)) == c);
    }
}
