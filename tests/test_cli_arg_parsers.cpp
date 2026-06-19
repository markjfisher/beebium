// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Unit tests for the inline CLI argument parsers in ServerMain.hpp.
// Specifically: tolerance for case in enum-style values, and quality of
// error messages when an unrecognised value is supplied.

#include <catch2/catch_test_macros.hpp>

#include <beebium/server/CliArgParsers.hpp>

#include <stdexcept>

using beebium::server::parse_sideways_arg;
using beebium::server::parse_wait_arg;
using beebium::server::parse_format_arg;
using beebium::server::parse_speed_arg;
using beebium::server::SidewaysSlotType;
using beebium::server::WaitMode;
using beebium::server::OutputFormat;

// ---- parse_sideways_arg ----

TEST_CASE("parse_sideways_arg accepts lowercase rom/ram/empty", "[cli][sideways]") {
    REQUIRE(parse_sideways_arg("4:rom:foo.rom").type == SidewaysSlotType::Rom);
    REQUIRE(parse_sideways_arg("4:ram").type == SidewaysSlotType::Ram);
    REQUIRE(parse_sideways_arg("4:empty").type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_sideways_arg accepts uppercase ROM/RAM/EMPTY", "[cli][sideways]") {
    REQUIRE(parse_sideways_arg("4:ROM:foo.rom").type == SidewaysSlotType::Rom);
    REQUIRE(parse_sideways_arg("4:RAM").type == SidewaysSlotType::Ram);
    REQUIRE(parse_sideways_arg("4:EMPTY").type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_sideways_arg accepts mixed-case values", "[cli][sideways]") {
    REQUIRE(parse_sideways_arg("4:Rom:foo.rom").type == SidewaysSlotType::Rom);
    REQUIRE(parse_sideways_arg("4:Ram").type == SidewaysSlotType::Ram);
    REQUIRE(parse_sideways_arg("4:Empty").type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_sideways_arg error message lists allowed values", "[cli][sideways]") {
    try {
        parse_sideways_arg("4:WAT");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        // The bad value, the flag name, and at least the three allowed values
        // must all appear in the message so the user can see what to type.
        REQUIRE(msg.find("WAT") != std::string::npos);
        REQUIRE(msg.find("--sideways") != std::string::npos);
        REQUIRE(msg.find("rom") != std::string::npos);
        REQUIRE(msg.find("ram") != std::string::npos);
        REQUIRE(msg.find("empty") != std::string::npos);
    }
}

TEST_CASE("parse_sideways_arg rejects bad slot number with clear message", "[cli][sideways]") {
    try {
        parse_sideways_arg("99:rom:foo.rom");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("99") != std::string::npos);
        REQUIRE(msg.find("0-15") != std::string::npos);
    }
}

// ---- parse_wait_arg ----

TEST_CASE("parse_wait_arg accepts lowercase cli/api", "[cli][wait]") {
    REQUIRE(parse_wait_arg("cli") == WaitMode::Cli);
    REQUIRE(parse_wait_arg("api") == WaitMode::Api);
}

TEST_CASE("parse_wait_arg accepts uppercase CLI/API", "[cli][wait]") {
    REQUIRE(parse_wait_arg("CLI") == WaitMode::Cli);
    REQUIRE(parse_wait_arg("API") == WaitMode::Api);
}

TEST_CASE("parse_wait_arg error message lists allowed values", "[cli][wait]") {
    try {
        parse_wait_arg("WAT");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("WAT") != std::string::npos);
        REQUIRE(msg.find("cli") != std::string::npos);
        REQUIRE(msg.find("api") != std::string::npos);
    }
}

// ---- parse_format_arg ----

TEST_CASE("parse_format_arg accepts mixed-case values", "[cli][format]") {
    REQUIRE(parse_format_arg("Pretty") == OutputFormat::Pretty);
    REQUIRE(parse_format_arg("TSV") == OutputFormat::Tsv);
    REQUIRE(parse_format_arg("JSONL") == OutputFormat::Jsonl);
}

TEST_CASE("parse_format_arg error message lists allowed values", "[cli][format]") {
    try {
        parse_format_arg("xml");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("xml") != std::string::npos);
        REQUIRE(msg.find("pretty") != std::string::npos);
        REQUIRE(msg.find("tsv") != std::string::npos);
        REQUIRE(msg.find("jsonl") != std::string::npos);
    }
}

// ---- parse_speed_arg ----

TEST_CASE("parse_speed_arg accepts positive multipliers", "[cli][speed]") {
    REQUIRE(parse_speed_arg("1") == 1.0);
    REQUIRE(parse_speed_arg("1.0") == 1.0);
    REQUIRE(parse_speed_arg("0.5") == 0.5);
    REQUIRE(parse_speed_arg("2.0") == 2.0);
}

TEST_CASE("parse_speed_arg maps the word 'unlimited' to the 0.0 sentinel", "[cli][speed]") {
    REQUIRE(parse_speed_arg("unlimited") == 0.0);
    REQUIRE(parse_speed_arg("UNLIMITED") == 0.0);
    REQUIRE(parse_speed_arg("Unlimited") == 0.0);
}

TEST_CASE("parse_speed_arg rejects zero and points at 'unlimited'", "[cli][speed]") {
    try {
        parse_speed_arg("0");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("0") != std::string::npos);
        REQUIRE(msg.find("unlimited") != std::string::npos);
    }
}

TEST_CASE("parse_speed_arg rejects negative multipliers", "[cli][speed]") {
    REQUIRE_THROWS_AS(parse_speed_arg("-1"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg("-0.5"), std::runtime_error);
}

TEST_CASE("parse_speed_arg rejects non-numeric and trailing junk", "[cli][speed]") {
    REQUIRE_THROWS_AS(parse_speed_arg("fast"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg("2x"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg(""), std::runtime_error);
}

TEST_CASE("parse_speed_arg rejects non-finite values", "[cli][speed]") {
    REQUIRE_THROWS_AS(parse_speed_arg("inf"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg("nan"), std::runtime_error);
}
