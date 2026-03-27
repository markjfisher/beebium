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

#include <beebium/extension/OneMHzBusPort.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

struct MockDevice : OneMHzBusDevice {
    uint8_t last_read_offset = 0;
    uint8_t last_write_offset = 0;
    uint8_t last_write_value = 0;
    int tick_count = 0;
    uint8_t read_value = 0x42;

    uint8_t read(uint16_t offset) override {
        last_read_offset = static_cast<uint8_t>(offset);
        return read_value;
    }

    void write(uint16_t offset, uint8_t value) override {
        last_write_offset = static_cast<uint8_t>(offset);
        last_write_value = value;
    }

    void tick() override {
        ++tick_count;
    }
};

}  // namespace

TEST_CASE("OneMHzBusPort empty reads return 0xFF", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    REQUIRE(port.read(0x00) == 0xFF);
    REQUIRE(port.read(0x50) == 0xFF);
    REQUIRE(port.read(0xFF) == 0xFF);
    REQUIRE(port.read(0x100) == 0xFF);  // JIM region
    REQUIRE(port.read(0x1FF) == 0xFF);
}

TEST_CASE("OneMHzBusPort empty writes do not crash", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    port.write(0x00, 0x42);
    port.write(0xFF, 0x42);
    port.write(0x1FF, 0x42);
}

TEST_CASE("OneMHzBusPort delegates reads to claimed device", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;
    device.read_value = 0xAB;

    port.claim_addresses(0x50, 0x57, device);

    REQUIRE(port.read(0x50) == 0xAB);
    REQUIRE(device.last_read_offset == 0x50);

    REQUIRE(port.read(0x57) == 0xAB);
    REQUIRE(device.last_read_offset == 0x57);
}

TEST_CASE("OneMHzBusPort delegates writes to claimed device", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    port.claim_addresses(0x50, 0x57, device);

    port.write(0x53, 0xCD);
    REQUIRE(device.last_write_offset == 0x53);
    REQUIRE(device.last_write_value == 0xCD);
}

TEST_CASE("OneMHzBusPort unclaimed addresses unaffected by claims", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    port.claim_addresses(0x50, 0x57, device);

    REQUIRE(port.read(0x4F) == 0xFF);
    REQUIRE(port.read(0x58) == 0xFF);
    REQUIRE(port.read(0x00) == 0xFF);
}

TEST_CASE("OneMHzBusPort rejects overlapping claims", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device1;
    MockDevice device2;

    port.claim_addresses(0x50, 0x57, device1);

    REQUIRE_THROWS_AS(
        port.claim_addresses(0x54, 0x5B, device2),
        std::runtime_error);
}

TEST_CASE("OneMHzBusPort rejects invalid address range", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    REQUIRE_THROWS_AS(
        port.claim_addresses(0x57, 0x50, device),  // reversed
        std::runtime_error);
}

TEST_CASE("OneMHzBusPort tick calls each device once", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device1;
    MockDevice device2;

    port.claim_addresses(0x50, 0x57, device1);
    port.claim_addresses(0x60, 0x67, device2);

    port.tick();

    REQUIRE(device1.tick_count == 1);
    REQUIRE(device2.tick_count == 1);

    port.tick();

    REQUIRE(device1.tick_count == 2);
    REQUIRE(device2.tick_count == 2);
}

TEST_CASE("OneMHzBusPort tick does not double-tick same device", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    // Same device claiming two non-contiguous ranges
    port.claim_addresses(0x50, 0x53, device);
    port.claim_addresses(0x60, 0x63, device);

    port.tick();

    REQUIRE(device.tick_count == 1);
}

TEST_CASE("OneMHzBusPort multiple devices coexist", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device1;
    MockDevice device2;
    device1.read_value = 0x11;
    device2.read_value = 0x22;

    port.claim_addresses(0x40, 0x43, device1);
    port.claim_addresses(0x50, 0x53, device2);

    REQUIRE(port.read(0x40) == 0x11);
    REQUIRE(port.read(0x50) == 0x22);
    REQUIRE(port.read(0x44) == 0xFF);  // gap between devices
}

TEST_CASE("OneMHzBusPort is_claimed query", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    REQUIRE_FALSE(port.is_claimed(0x50));
    port.claim_addresses(0x50, 0x57, device);
    REQUIRE(port.is_claimed(0x50));
    REQUIRE(port.is_claimed(0x57));
    REQUIRE_FALSE(port.is_claimed(0x58));
}
