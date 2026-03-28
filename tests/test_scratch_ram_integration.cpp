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

#include <beebium/ModelBHardware.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <TestScratchRam.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium;

TEST_CASE("TestScratchRam accessible through ModelBHardware memory map",
          "[extension][scratch-ram][integration]") {
    ModelBHardware hw;

    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    registry.register_extension(std::make_unique<TestScratchRam>());

    ExtensionContext ctx(&hw.one_mhz_bus());
    registry.resolve_and_init(ctx);

    SECTION("Write and read back through hardware memory map") {
        hw.write(0xFC50, 0x42);
        REQUIRE(hw.read(0xFC50) == 0x42);
    }

    SECTION("All 8 bytes accessible through memory map") {
        for (uint16_t i = 0; i < 8; ++i) {
            hw.write(0xFC50 + i, static_cast<uint8_t>(0xA0 + i));
        }
        for (uint16_t i = 0; i < 8; ++i) {
            REQUIRE(hw.read(0xFC50 + i) == static_cast<uint8_t>(0xA0 + i));
        }
    }

    SECTION("Addresses outside scratch RAM still return 0xFF") {
        hw.write(0xFC50, 0x42);  // prove writes work
        REQUIRE(hw.read(0xFC4F) == 0xFF);
        REQUIRE(hw.read(0xFC58) == 0xFF);
        REQUIRE(hw.read(0xFC00) == 0xFF);
        REQUIRE(hw.read(0xFDFF) == 0xFF);
    }

    registry.shutdown();
}

TEST_CASE("ModelBHardware FRED/JIM returns 0xFF without extensions",
          "[extension][1mhz-bus][integration]") {
    ModelBHardware hw;

    // No extensions registered -- all FRED/JIM should be open bus
    REQUIRE(hw.read(0xFC00) == 0xFF);
    REQUIRE(hw.read(0xFC50) == 0xFF);
    REQUIRE(hw.read(0xFD00) == 0xFF);
    REQUIRE(hw.read(0xFDFF) == 0xFF);
}
