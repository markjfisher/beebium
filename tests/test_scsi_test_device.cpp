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
#include <ScsiTestDevice.hpp>
#include <ScsiConstants.hpp>

using namespace beebium;
using namespace beebium::scsi;

TEST_CASE("ScsiTestDevice not present by default", "[scsi][test-device]") {
    ScsiTestDevice dev;
    REQUIRE_FALSE(dev.is_present());
}

TEST_CASE("ScsiTestDevice present when configured", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);
    REQUIRE(dev.is_present());
}

TEST_CASE("ScsiTestDevice records commands", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    dev.execute(cdb, {});

    REQUIRE(dev.command_count() == 1);
    REQUIRE(dev.recorded_commands()[0].cdb.size() == 6);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_TEST_UNIT_READY);
}

TEST_CASE("ScsiTestDevice records multiple commands", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    uint8_t cdb1[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    uint8_t cdb2[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    dev.execute(cdb1, {});
    dev.execute(cdb2, {});

    REQUIRE(dev.command_count() == 2);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_TEST_UNIT_READY);
    REQUIRE(dev.recorded_commands()[1].cdb[0] == OP_INQUIRY);
}

TEST_CASE("ScsiTestDevice clear recorded commands", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    dev.execute(cdb, {});
    REQUIRE(dev.command_count() == 1);

    dev.clear_recorded_commands();
    REQUIRE(dev.command_count() == 0);
}

TEST_CASE("ScsiTestDevice configurable status", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_default_status(STATUS_CHECK_CONDITION);

    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.status == STATUS_CHECK_CONDITION);
}

TEST_CASE("ScsiTestDevice returns inquiry data", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    // Default inquiry
    uint8_t cdb[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 36);
    REQUIRE(result.data_in[0] == 0x00);  // Direct access device
}

TEST_CASE("ScsiTestDevice configurable inquiry response", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_inquiry_response({0x05, 0x80, 0x02});  // CD-ROM, removable, SCSI-2

    uint8_t cdb[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.data_in.size() == 3);
    REQUIRE(result.data_in[0] == 0x05);
}

TEST_CASE("ScsiTestDevice READ(6) returns configured data", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    std::vector<uint8_t> sector(256, 0xAA);
    dev.set_read_data(sector);

    // READ(6): 1 block from LBA 0
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 1, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 256);
    REQUIRE(result.data_in[0] == 0xAA);
}

TEST_CASE("ScsiTestDevice WRITE(6) expects data", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    // First call without data -- should request data_out
    uint8_t cdb[] = {OP_WRITE_6, 0, 0, 0, 1, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.data_out_expected == 256);
}

TEST_CASE("ScsiTestDevice WRITE(6) records data_out", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    std::vector<uint8_t> data(256, 0xBB);
    uint8_t cdb[] = {OP_WRITE_6, 0, 0, 0, 1, 0};
    auto result = dev.execute(cdb, data);

    REQUIRE(dev.recorded_commands().back().data_out.size() == 256);
    REQUIRE(dev.recorded_commands().back().data_out[0] == 0xBB);
}

TEST_CASE("ScsiTestDevice REQUEST SENSE returns sense data", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_sense_data({0x70, 0x00, 0x05, 0x00});  // Illegal request

    uint8_t cdb[] = {OP_REQUEST_SENSE, 0, 0, 0, 18, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.data_in.size() == 4);
    REQUIRE(result.data_in[0] == 0x70);
    REQUIRE(result.data_in[2] == 0x05);
}

TEST_CASE("ScsiTestDevice READ CAPACITY returns capacity", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_capacity(1000, 256);

    uint8_t cdb[] = {OP_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.data_in.size() == 8);
    // Last LBA = 999 = 0x000003E7
    REQUIRE(result.data_in[0] == 0x00);
    REQUIRE(result.data_in[1] == 0x00);
    REQUIRE(result.data_in[2] == 0x03);
    REQUIRE(result.data_in[3] == 0xE7);
    // Block size = 256 = 0x00000100
    REQUIRE(result.data_in[4] == 0x00);
    REQUIRE(result.data_in[5] == 0x00);
    REQUIRE(result.data_in[6] == 0x01);
    REQUIRE(result.data_in[7] == 0x00);
}

TEST_CASE("ScsiTestDevice TRANSLATE returns LBA in little-endian", "[scsi][test-device]") {
    ScsiTestDevice dev;
    dev.set_present(true);

    // LBA = (0x01 & 0x1F) << 16 | 0x23 << 8 | 0x45 = 0x012345
    uint8_t cdb[] = {OP_TRANSLATE, 0x01, 0x23, 0x45, 0, 0};
    auto result = dev.execute(cdb, {});

    REQUIRE(result.data_in.size() == 4);
    REQUIRE(result.data_in[0] == 0x45);  // LSB
    REQUIRE(result.data_in[1] == 0x23);
    REQUIRE(result.data_in[2] == 0x01);
    REQUIRE(result.data_in[3] == 0x00);  // MSB
}

TEST_CASE("ScsiTestDevice device type and description", "[scsi][test-device]") {
    ScsiTestDevice dev;
    REQUIRE(dev.device_type() == "test-device");
    REQUIRE(dev.description() == "SCSI test device");

    dev.set_description("Custom test device");
    REQUIRE(dev.description() == "Custom test device");
}
