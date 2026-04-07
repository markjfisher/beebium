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

// Integration tests for the Acorn 65C02 coprocessor extension.
//
// These tests create the extension, provide it with a TubeSocket via
// ExtensionContext, and verify that the parasite boots and communicates
// with the host through the TubeUla bridge -- all in-process with the
// parasite running in its own thread.

#include <catch2/catch_test_macros.hpp>

#include "SecondProcessor65C02Extension.hpp"
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <chrono>
#include <thread>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;

TEST_CASE("65C02 extension: boots and produces R1 banner", "[tube][extension]") {
    // Set up a TubeSocket (as the host machine would have).
    TubeSocket tube_socket;

    // Create ExtensionContext with the TubeSocket.
    ExtensionContext ctx(nullptr, nullptr, &tube_socket);

    // Create and configure the extension.
    SecondProcessor65C02Extension ext;
    ext.set_config({
        {"id", "test-tube"},
        {"rom", std::string(BEEBIUM_ROM_DIR) + "/acorn-tube-6502_1_10.rom"}
    });

    // Initialise -- this starts the parasite thread.
    ext.init(ctx);
    REQUIRE(ext.running());
    REQUIRE(tube_socket.enabled());

    // Wait for the boot banner to fill the R1 FIFO (24 bytes).
    // The parasite writes the banner character-by-character via OSWRCH,
    // which takes ~100K cycles. Poll with peek (side-effect-free) until
    // the runner has completed enough cycles for the full banner.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ext.runner()->cycle_count() >= 100000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    uint8_t status = tube_socket.peek(0);
    REQUIRE((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read the 24-byte banner through the host's TubeSocket interface.
    static constexpr uint8_t expected_banner[] = {
        0x0A,
        'A', 'c', 'o', 'r', 'n', ' ',
        'T', 'U', 'B', 'E', ' ',
        '6', '5', '0', '2', ' ',
        '6', '4', 'K',
        0x0A, 0x0A, 0x0D, 0x00
    };
    static_assert(sizeof(expected_banner) == 24);

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(tube_socket.read(1) == expected_banner[i]);
    }

    // Shutdown the extension.
    ext.shutdown();
    CHECK(!ext.running());
    CHECK(!tube_socket.enabled());
}

TEST_CASE("65C02 extension: cross-processor stop via counterpart callback", "[tube][extension]") {
    TubeSocket tube_socket;
    ExtensionContext ctx(nullptr, nullptr, &tube_socket);

    SecondProcessor65C02Extension ext;
    ext.set_config({
        {"id", "test-tube-xstop"},
        {"rom", std::string(BEEBIUM_ROM_DIR) + "/acorn-tube-6502_1_10.rom"}
    });
    ext.init(ctx);
    REQUIRE(ext.running());

    // Wait for boot.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ext.runner()->cycle_count() >= 100000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Simulate cross-processor stop: calling the parasite_pause_callback
    // should pause the parasite runner.
    REQUIRE(!ext.runner()->is_paused());
    auto pause_cb = ext.parasite_pause_callback();
    pause_cb();

    // Give the runner time to observe the pause flag and stop.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(ext.runner()->is_paused());

    // Resume and verify it continues.
    ext.runner()->resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!ext.runner()->is_paused());

    ext.shutdown();
}

TEST_CASE("65C02 extension: shutdown is idempotent", "[tube][extension]") {
    TubeSocket tube_socket;
    ExtensionContext ctx(nullptr, nullptr, &tube_socket);

    SecondProcessor65C02Extension ext;
    ext.set_config({
        {"id", "test-tube-2"},
        {"rom", std::string(BEEBIUM_ROM_DIR) + "/acorn-tube-6502_1_10.rom"}
    });

    ext.init(ctx);
    REQUIRE(ext.running());

    ext.shutdown();
    CHECK(!ext.running());

    // Second shutdown is a no-op.
    ext.shutdown();
    CHECK(!ext.running());
}
