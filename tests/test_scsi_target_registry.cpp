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

#include <catch2/catch_test_macros.hpp>
#include <ScsiTargetRegistry.hpp>
#include <ScsiTestDevice.hpp>
#include <ScsiBus.hpp>

using namespace beebium;

TEST_CASE("ScsiTargetRegistry starts empty", "[scsi][registry]") {
    ScsiTargetRegistry reg;
    for (uint8_t id = 0; id < scsi::MAX_TARGETS; ++id) {
        REQUIRE_FALSE(reg.has_target(id));
        REQUIRE(reg.target(id) == nullptr);
    }
}

TEST_CASE("ScsiTargetRegistry install and query", "[scsi][registry]") {
    ScsiTargetRegistry reg;
    auto dev = std::make_unique<ScsiTestDevice>();
    dev->set_present(true);
    auto* raw = dev.get();

    reg.install(4, std::move(dev));

    REQUIRE(reg.has_target(4));
    REQUIRE(reg.target(4) == raw);
    REQUIRE_FALSE(reg.has_target(3));
    REQUIRE_FALSE(reg.has_target(5));
}

TEST_CASE("ScsiTargetRegistry remove returns ownership", "[scsi][registry]") {
    ScsiTargetRegistry reg;
    auto dev = std::make_unique<ScsiTestDevice>();
    reg.install(2, std::move(dev));
    REQUIRE(reg.has_target(2));

    auto removed = reg.remove(2);
    REQUIRE(removed != nullptr);
    REQUIRE_FALSE(reg.has_target(2));
}

TEST_CASE("ScsiTargetRegistry enumerate lists installed targets", "[scsi][registry]") {
    ScsiTargetRegistry reg;

    auto dev0 = std::make_unique<ScsiTestDevice>();
    dev0->set_present(true);
    dev0->set_description("Drive 0");
    reg.install(0, std::move(dev0));

    auto dev3 = std::make_unique<ScsiTestDevice>();
    dev3->set_present(false);
    dev3->set_description("Drive 3");
    reg.install(3, std::move(dev3));

    auto infos = reg.enumerate();
    REQUIRE(infos.size() == 2);

    REQUIRE(infos[0].id == 0);
    REQUIRE(infos[0].present == true);
    REQUIRE(infos[0].device_type == "test-device");
    REQUIRE(infos[0].description == "Drive 0");

    REQUIRE(infos[1].id == 3);
    REQUIRE(infos[1].present == false);
    REQUIRE(infos[1].description == "Drive 3");
}

TEST_CASE("ScsiTargetRegistry wire_to_bus connects targets", "[scsi][registry]") {
    ScsiTargetRegistry reg;
    ScsiBus bus;

    auto dev = std::make_unique<ScsiTestDevice>();
    dev->set_present(true);
    auto* raw = dev.get();
    reg.install(5, std::move(dev));

    reg.wire_to_bus(bus);

    REQUIRE(bus.target(5) == raw);
    REQUIRE(bus.target(0) == nullptr);
}
