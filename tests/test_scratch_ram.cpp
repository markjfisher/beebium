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

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/TestScratchRam.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium;

TEST_CASE("TestScratchRam zero-initialised", "[extension][scratch-ram]") {
    OneMHzBusPort port;
    ExtensionContext ctx(&port);
    TestScratchRam ram;
    ram.init(ctx);

    for (uint16_t i = 0; i < TestScratchRam::kSize; ++i) {
        REQUIRE(ram.peek(i) == 0x00);
    }
}

TEST_CASE("TestScratchRam write and read back", "[extension][scratch-ram]") {
    OneMHzBusPort port;
    ExtensionContext ctx(&port);
    TestScratchRam ram;
    ram.init(ctx);

    port.write(0x50, 0x42);
    REQUIRE(port.read(0x50) == 0x42);

    port.write(0x57, 0xAB);
    REQUIRE(port.read(0x57) == 0xAB);
}

TEST_CASE("TestScratchRam all 8 bytes independent", "[extension][scratch-ram]") {
    OneMHzBusPort port;
    ExtensionContext ctx(&port);
    TestScratchRam ram;
    ram.init(ctx);

    for (uint16_t i = 0; i < TestScratchRam::kSize; ++i) {
        port.write(TestScratchRam::kBaseOffset + i, static_cast<uint8_t>(i + 1));
    }

    for (uint16_t i = 0; i < TestScratchRam::kSize; ++i) {
        REQUIRE(port.read(TestScratchRam::kBaseOffset + i) == static_cast<uint8_t>(i + 1));
    }
}

TEST_CASE("TestScratchRam does not affect adjacent addresses",
          "[extension][scratch-ram]") {
    OneMHzBusPort port;
    ExtensionContext ctx(&port);
    TestScratchRam ram;
    ram.init(ctx);

    port.write(0x50, 0xFF);

    REQUIRE(port.read(0x4F) == 0xFF);  // unclaimed, returns open bus 0xFF
    REQUIRE(port.read(0x58) == 0xFF);  // unclaimed, returns open bus 0xFF
}

TEST_CASE("TestScratchRam name and dependencies", "[extension][scratch-ram]") {
    TestScratchRam ram;

    REQUIRE(ram.name() == "test-scratch-ram");
    REQUIRE(ram.attaches_to().size() == 1);
    REQUIRE(ram.attaches_to()[0] == "1mhz-bus");
    REQUIRE(ram.provides().empty());
}
