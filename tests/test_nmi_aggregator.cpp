// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Test compile-time NMI aggregation system

#include <catch2/catch_test_macros.hpp>
#include <beebium/NmiAggregator.hpp>
#include <beebium/disc/DiscDrive.hpp>
#include <beebium/disc/MemoryDiscImage.hpp>
#include <beebium/disc/WD1770.hpp>

using namespace beebium;

namespace {

// Test device that generates NMIs
struct TestNmiDevice {
    bool nmi_active = false;

    bool nmi_pending() const { return nmi_active; }
};

} // anonymous namespace

TEST_CASE("NmiSource concept", "[nmi]") {
    STATIC_REQUIRE(NmiSource<TestNmiDevice>);
    STATIC_REQUIRE(NmiSource<WD1770>);
}

TEST_CASE("Single NMI source", "[nmi]") {
    TestNmiDevice device;
    auto aggregator = make_nmi_aggregator(make_nmi_binding<0>(device));

    // No NMI pending
    device.nmi_active = false;
    CHECK(aggregator.poll() == 0x00);

    // NMI pending
    device.nmi_active = true;
    CHECK(aggregator.poll() == 0x01);
}

TEST_CASE("Multiple NMI sources", "[nmi]") {
    TestNmiDevice device1;
    TestNmiDevice device2;

    auto aggregator = make_nmi_aggregator(
        make_nmi_binding<0>(device1),
        make_nmi_binding<1>(device2)
    );

    // No NMIs pending
    CHECK(aggregator.poll() == 0x00);

    // Only device1 NMI
    device1.nmi_active = true;
    CHECK(aggregator.poll() == 0x01);

    // Both devices
    device2.nmi_active = true;
    CHECK(aggregator.poll() == 0x03);

    // Clear device1
    device1.nmi_active = false;
    CHECK(aggregator.poll() == 0x02);

    // Clear all
    device2.nmi_active = false;
    CHECK(aggregator.poll() == 0x00);
}

TEST_CASE("NMI bit positions", "[nmi]") {
    TestNmiDevice device;

    SECTION("Bit 0") {
        auto agg = make_nmi_aggregator(make_nmi_binding<0>(device));
        device.nmi_active = true;
        CHECK(agg.poll() == 0x01);
    }

    SECTION("Bit 3") {
        auto agg = make_nmi_aggregator(make_nmi_binding<3>(device));
        device.nmi_active = true;
        CHECK(agg.poll() == 0x08);
    }
}

TEST_CASE("NullNmiAggregator", "[nmi]") {
    NullNmiAggregator aggregator;

    // Always returns 0
    CHECK(aggregator.poll() == 0x00);
    CHECK(aggregator.size() == 0);
}

TEST_CASE("WD1770 nmi_pending reflects INTRQ", "[nmi][wd1770]") {
    WD1770 controller;
    DiscDrive drive;
    auto disc = MemoryDiscImage::create_ssd();
    drive.insert(std::move(disc));
    controller.attach_drive(0, &drive);
    controller.set_drive(0);

    // Initially no NMI
    CHECK_FALSE(controller.nmi_pending());

    // Issue a command that will complete and set INTRQ
    controller.write(0, 0xD0);  // Force Interrupt
    CHECK(controller.nmi_pending());

    // Reading status clears INTRQ
    controller.read(0);
    CHECK_FALSE(controller.nmi_pending());
}

TEST_CASE("NMI aggregator with WD1770", "[nmi][wd1770]") {
    WD1770 controller;
    auto aggregator = make_nmi_aggregator(make_nmi_binding<0>(controller));

    // Initially no NMI
    CHECK(aggregator.poll() == 0x00);

    // Issue Force Interrupt to set INTRQ
    controller.write(0, 0xD0);
    CHECK(aggregator.poll() == 0x01);

    // Reading status clears INTRQ
    controller.read(0);
    CHECK(aggregator.poll() == 0x00);
}
