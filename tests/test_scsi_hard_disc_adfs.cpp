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

// Tests using a real ADFS-formatted SCSI disc image (from BeebEm).
// These verify that our HardDiskImage and ScsiHardDisc correctly read
// a known-good image produced by another emulator.

#include <catch2/catch_test_macros.hpp>
#include <HardDiskImage.hpp>
#include <ScsiHardDisc.hpp>
#include <ScsiBus.hpp>
#include <ScsiConstants.hpp>

#include <filesystem>
#include <string>

using namespace beebium;
using namespace beebium::scsi;

namespace {

#ifdef BEEBIUM_TEST_ASSETS_DIR
    const std::filesystem::path kAssetsDir = BEEBIUM_TEST_ASSETS_DIR;
#else
    const std::filesystem::path kAssetsDir;
#endif

std::filesystem::path scsi_asset(const std::string& filename) {
    return kAssetsDir / "scsi" / filename;
}

}  // namespace

// ---------------------------------------------------------------------------
// HardDiskImage with real ADFS image
// ---------------------------------------------------------------------------

TEST_CASE("HardDiskImage opens BeebEm ADFS SCSI image", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    REQUIRE(image != nullptr);
    REQUIRE(image->cylinders() == 306);
    REQUIRE(image->heads() == 4);
    REQUIRE(image->total_sectors() == 40392);
}

TEST_CASE("HardDiskImage reads ADFS free space map from sector 0", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    REQUIRE(image->read_sector(sector.data(), 0));

    // ADFS free space map: disc size at offset 0xFC (3 bytes LE)
    // For this image: 0x489d00 sectors... actually the value at 0xFC
    // is part of the ADFS free space map checksum/size area
    // Just verify sector 0 is not all zeros (it's a formatted disc)
    bool all_zeros = true;
    for (auto byte : sector) {
        if (byte != 0) { all_zeros = false; break; }
    }
    REQUIRE_FALSE(all_zeros);
}

TEST_CASE("HardDiskImage reads ADFS root directory Hugo signature", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));

    // ADFS root directory is at sector 2 (offset 0x200)
    // "Hugo" signature at byte 1 of the directory (offset 0x201 in file = sector 2, byte 1)
    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    REQUIRE(image->read_sector(sector.data(), 2));

    // "Hugo" at byte 1 of sector 2
    REQUIRE(sector[1] == 'H');
    REQUIRE(sector[2] == 'u');
    REQUIRE(sector[3] == 'g');
    REQUIRE(sector[4] == 'o');
}

// ---------------------------------------------------------------------------
// ScsiHardDisc with real ADFS image
// ---------------------------------------------------------------------------

TEST_CASE("ScsiHardDisc reads ADFS root directory via READ(6)", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    ScsiHardDisc disc(std::move(image));

    // READ(6): 1 sector from LBA 2 (ADFS root directory)
    uint8_t cdb[] = {OP_READ_6, 0, 0, 2, 1, 0};
    auto result = disc.execute(cdb, {});

    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 256);

    // "Hugo" at byte 1
    REQUIRE(result.data_in[1] == 'H');
    REQUIRE(result.data_in[2] == 'u');
    REQUIRE(result.data_in[3] == 'g');
    REQUIRE(result.data_in[4] == 'o');
}

TEST_CASE("ScsiHardDisc INQUIRY identifies as direct-access device", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    ScsiHardDisc disc(std::move(image));

    uint8_t cdb[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    auto result = disc.execute(cdb, {});

    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in[0] == 0x00);  // Direct access device
}

TEST_CASE("ScsiHardDisc READ CAPACITY matches image geometry", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    ScsiHardDisc disc(std::move(image));

    uint8_t cdb[] = {OP_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto result = disc.execute(cdb, {});

    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 8);

    // Last LBA = 40391 = 0x00009DC7
    uint32_t last_lba = (static_cast<uint32_t>(result.data_in[0]) << 24)
                      | (static_cast<uint32_t>(result.data_in[1]) << 16)
                      | (static_cast<uint32_t>(result.data_in[2]) << 8)
                      | result.data_in[3];
    REQUIRE(last_lba == 40391);

    // Block size = 256
    uint32_t block_size = (static_cast<uint32_t>(result.data_in[4]) << 24)
                        | (static_cast<uint32_t>(result.data_in[5]) << 16)
                        | (static_cast<uint32_t>(result.data_in[6]) << 8)
                        | result.data_in[7];
    REQUIRE(block_size == 256);
}

TEST_CASE("ScsiHardDisc MODE SENSE returns correct geometry", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    ScsiHardDisc disc(std::move(image));

    uint8_t cdb[] = {OP_MODE_SENSE_6, 0, 0, 0, 22, 0};
    auto result = disc.execute(cdb, {});

    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 22);

    // Cylinders = 306 = 0x0132 (big-endian at bytes 13-14)
    uint16_t cylinders = (static_cast<uint16_t>(result.data_in[13]) << 8)
                       | result.data_in[14];
    REQUIRE(cylinders == 306);

    // Heads = 4
    REQUIRE(result.data_in[15] == 4);
}

// ---------------------------------------------------------------------------
// Full SCSI bus transaction with real ADFS image
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus READ(6) of ADFS root directory through bus protocol", "[scsi][adfs][asset]") {
    if (kAssetsDir.empty()) SKIP("BEEBIUM_TEST_ASSETS_DIR not set");

    auto image = HardDiskImage::open(scsi_asset("scsi0.dat"));
    auto disc = std::make_unique<ScsiHardDisc>(std::move(image));

    ScsiBus bus;
    bus.set_target(0, disc.get());

    // Select target 0
    bus.write_register(REG_DATA, 0x01);
    REQUIRE(bus.phase() == ScsiBusPhase::Selection);
    bus.write_register(REG_SELECT, 0);
    REQUIRE(bus.phase() == ScsiBusPhase::Command);

    // READ(6): 1 sector from LBA 2 (root directory)
    uint8_t cdb[] = {OP_READ_6, 0, 0, 2, 1, 0};
    for (auto byte : cdb) {
        bus.write_register(REG_DATA, byte);
    }
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    // Read 256 bytes
    std::vector<uint8_t> data;
    while (bus.phase() == ScsiBusPhase::DataIn) {
        data.push_back(bus.read_register(REG_DATA));
    }
    REQUIRE(data.size() == 256);

    // Verify "Hugo" at byte 1
    REQUIRE(data[1] == 'H');
    REQUIRE(data[2] == 'u');
    REQUIRE(data[3] == 'g');
    REQUIRE(data[4] == 'o');

    // Complete transaction
    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    uint8_t status = bus.read_register(REG_DATA);
    REQUIRE(status == STATUS_GOOD);
    bus.read_register(REG_DATA);  // message
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);
}
