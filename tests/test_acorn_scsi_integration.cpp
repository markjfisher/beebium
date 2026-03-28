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

// Integration tests: AcornScsiHostAdapter through the ModelBHardware memory map.
// Verifies that 6502-style reads/writes to 0xFC40-0xFC43 correctly interact
// with the SCSI bus protocol.

#include <catch2/catch_test_macros.hpp>

#include <beebium/ModelBHardware.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <AcornScsiHostAdapter.hpp>
#include <ScsiTestDevice.hpp>
#include <ScsiConstants.hpp>

using namespace beebium;
using namespace beebium::scsi;

namespace {

struct ScsiIntegrationFixture {
    ModelBHardware hw;
    ExtensionRegistry registry;
    AcornScsiHostAdapter* adapter = nullptr;

    ScsiIntegrationFixture() {
        registry.register_extension_point("1mhz-bus");
        auto ext = AcornScsiHostAdapter::create();
        adapter = ext.get();
        registry.register_extension(std::move(ext));

        ExtensionContext ctx(&hw.one_mhz_bus());
        registry.resolve_and_init(ctx);
    }

    ~ScsiIntegrationFixture() {
        registry.shutdown();
    }

    void install_test_device(uint8_t id) {
        auto dev = std::make_unique<ScsiTestDevice>();
        dev->set_present(true);
        adapter->target_registry().install(id, std::move(dev));
        adapter->target_registry().wire_to_bus(adapter->bus());
    }
};

}  // namespace

TEST_CASE("AcornScsiHostAdapter accessible through ModelBHardware",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;

    // Status register should be readable at 0xFC41
    REQUIRE(f.hw.read(0xFC41) == 0x00);  // BUS_FREE: all bits clear
}

TEST_CASE("AcornScsiHostAdapter claims correct address range",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;

    // Adjacent addresses should return open bus (0xFF)
    REQUIRE(f.hw.read(0xFC3F) == 0xFF);
    REQUIRE(f.hw.read(0xFC44) == 0xFF);
}

TEST_CASE("AcornScsiHostAdapter name and extension points",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;

    REQUIRE(f.adapter->name() == "acorn-scsi");
    REQUIRE(f.adapter->attaches_to().size() == 1);
    REQUIRE(f.adapter->attaches_to()[0] == "1mhz-bus");
    REQUIRE(f.adapter->provides().size() == 1);
    REQUIRE(f.adapter->provides()[0] == "scsi");
}

TEST_CASE("AcornScsiHostAdapter complete TEST UNIT READY through memory map",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;
    f.install_test_device(0);

    // Select target 0: write to data register (0xFC40) with bit 0 set
    f.hw.write(0xFC40, 0x01);

    // Verify selection phase via status register
    uint8_t sr = f.hw.read(0xFC41);
    REQUIRE((sr & SR_BSY) != 0);

    // Deassert SEL: write to select register (0xFC42)
    f.hw.write(0xFC42, 0x00);

    // Now in COMMAND phase -- write 6-byte TEST UNIT READY CDB
    f.hw.write(0xFC40, OP_TEST_UNIT_READY);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);

    // Should be in STATUS phase
    sr = f.hw.read(0xFC41);
    REQUIRE((sr & SR_CD) != 0);
    REQUIRE((sr & SR_IO) != 0);

    // Read status byte
    uint8_t status = f.hw.read(0xFC40);
    REQUIRE(status == STATUS_GOOD);

    // Read message byte
    uint8_t message = f.hw.read(0xFC40);
    REQUIRE(message == MSG_COMMAND_COMPLETE);

    // Back to BUS_FREE
    REQUIRE(f.hw.read(0xFC41) == 0x00);
}

TEST_CASE("AcornScsiHostAdapter READ(6) through memory map",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;
    f.install_test_device(0);

    // Configure test device with known data
    auto* dev = static_cast<ScsiTestDevice*>(f.adapter->target_registry().target(0));
    std::vector<uint8_t> sector(256);
    for (int i = 0; i < 256; ++i) sector[i] = static_cast<uint8_t>(i);
    dev->set_read_data(sector);

    // Select target 0
    f.hw.write(0xFC40, 0x01);
    f.hw.write(0xFC42, 0x00);

    // Write READ(6) CDB: 1 block from LBA 0
    f.hw.write(0xFC40, OP_READ_6);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x01);  // 1 block
    f.hw.write(0xFC40, 0x00);

    // Should be in DATA_IN -- read 256 bytes
    uint8_t sr = f.hw.read(0xFC41);
    REQUIRE((sr & SR_IO) != 0);  // Data In
    REQUIRE((sr & SR_CD) == 0);  // Data, not command

    for (int i = 0; i < 256; ++i) {
        uint8_t byte = f.hw.read(0xFC40);
        REQUIRE(byte == static_cast<uint8_t>(i));
    }

    // Status + Message + BUS_FREE
    f.hw.read(0xFC40);  // status
    f.hw.read(0xFC40);  // message
    REQUIRE(f.hw.read(0xFC41) == 0x00);  // BUS_FREE
}
