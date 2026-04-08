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
// with the host through the TubeUla bridge. The parasite is ticked
// via TubeSocket::tick_parasite() in the single-threaded model.

#include <catch2/catch_test_macros.hpp>

#include "SecondProcessor65C02Extension.hpp"
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <cstdint>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;

// Tick the parasite via the TubeSocket until the target cycle count is reached
// or the tick limit is exceeded. Each tick_parasite() call produces 1 or 2
// parasite cycles (3:2 fractional accumulator).
static void tick_parasite_until(TubeSocket& socket, ParasiteRunner& runner,
                                uint64_t target_cycles, int max_ticks = 200000)
{
    for (int i = 0; i < max_ticks && runner.cycle_count() < target_cycles; ++i) {
        socket.tick_parasite();
    }
}

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

    // Initialise -- installs parasite for single-threaded ticking.
    ext.init(ctx);
    REQUIRE(ext.running());
    REQUIRE(tube_socket.enabled());

    // Tick the parasite until it has completed enough cycles for the boot
    // banner (the parasite writes 24 bytes to the R1 P-to-H FIFO via OSWRCH,
    // which takes ~100K parasite cycles).
    tick_parasite_until(tube_socket, *ext.runner(), 100000);

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

    // Boot the parasite.
    tick_parasite_until(tube_socket, *ext.runner(), 100000);

    // Simulate cross-processor stop: calling the parasite_pause_callback
    // should pause the parasite runner.
    REQUIRE(!ext.runner()->is_paused());
    auto pause_cb = ext.parasite_pause_callback();
    pause_cb();
    CHECK(ext.runner()->is_paused());

    // Ticking while paused should be a no-op.
    auto cycles_before = ext.runner()->cycle_count();
    for (int i = 0; i < 1000; ++i) {
        tube_socket.tick_parasite();
    }
    CHECK(ext.runner()->cycle_count() == cycles_before);

    // Resume and verify ticking resumes.
    ext.runner()->resume();
    CHECK(!ext.runner()->is_paused());
    for (int i = 0; i < 1000; ++i) {
        tube_socket.tick_parasite();
    }
    CHECK(ext.runner()->cycle_count() > cycles_before);

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
