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
#include <ScsiBus.hpp>
#include <ScsiTestDevice.hpp>
#include <ScsiConstants.hpp>

using namespace beebium;
using namespace beebium::scsi;

namespace {

// Helper: select target 0 and transition to COMMAND phase
void select_target(ScsiBus& bus, uint8_t id = 0) {
    // Write data register with target bit (sel asserted via write to reg 0)
    bus.write_register(REG_DATA, 1 << id);
    REQUIRE(bus.phase() == ScsiBusPhase::Selection);
    // Write select register to deassert SEL and enter COMMAND
    bus.write_register(REG_SELECT, 0);
    REQUIRE(bus.phase() == ScsiBusPhase::Command);
}

// Helper: write a complete CDB
void write_cdb(ScsiBus& bus, std::span<const uint8_t> cdb) {
    for (auto byte : cdb) {
        bus.write_register(REG_DATA, byte);
    }
}

// Helper: read all data bytes from DATA_IN phase
std::vector<uint8_t> read_all_data(ScsiBus& bus) {
    std::vector<uint8_t> data;
    while (bus.phase() == ScsiBusPhase::DataIn) {
        data.push_back(bus.read_register(REG_DATA));
    }
    return data;
}

// Helper: complete status and message phases, returning to BUS_FREE
void complete_transaction(ScsiBus& bus) {
    if (bus.phase() == ScsiBusPhase::Status) {
        bus.read_register(REG_DATA);  // read status byte
    }
    if (bus.phase() == ScsiBusPhase::MessageIn) {
        bus.read_register(REG_DATA);  // read message byte
    }
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);
}

}  // namespace

// ---------------------------------------------------------------------------
// Bus state and phase transitions
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus starts in BUS_FREE phase", "[scsi][bus]") {
    ScsiBus bus;
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);
}

TEST_CASE("ScsiBus status register all zeros in BUS_FREE", "[scsi][bus]") {
    ScsiBus bus;
    REQUIRE(bus.status_register() == 0x00);
}

TEST_CASE("ScsiBus selection with no target stays BUS_FREE", "[scsi][bus]") {
    ScsiBus bus;
    bus.write_register(REG_DATA, 0x01);  // try to select target 0
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);
}

TEST_CASE("ScsiBus selection with target transitions to COMMAND", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    bus.write_register(REG_DATA, 0x01);  // select target 0
    REQUIRE(bus.phase() == ScsiBusPhase::Selection);
    REQUIRE(bus.selected_target_id() == 0);

    bus.write_register(REG_SELECT, 0);  // deassert SEL
    REQUIRE(bus.phase() == ScsiBusPhase::Command);
}

TEST_CASE("ScsiBus selection by target ID", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);

    SECTION("Target 0") {
        bus.set_target(0, &dev);
        bus.write_register(REG_DATA, 0x01);
        REQUIRE(bus.selected_target_id() == 0);
    }
    SECTION("Target 3") {
        bus.set_target(3, &dev);
        bus.write_register(REG_DATA, 0x08);
        REQUIRE(bus.selected_target_id() == 3);
    }
    SECTION("Target 7") {
        bus.set_target(7, &dev);
        bus.write_register(REG_DATA, 0x80);
        REQUIRE(bus.selected_target_id() == 7);
    }
}

TEST_CASE("ScsiBus selection of non-present target stays BUS_FREE", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(false);  // installed but not present
    bus.set_target(0, &dev);

    bus.write_register(REG_DATA, 0x01);
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);
}

// ---------------------------------------------------------------------------
// Status register reflects correct phase bits
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus status register in COMMAND phase", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);
    uint8_t sr = bus.status_register();
    REQUIRE((sr & SR_BSY) != 0);
    REQUIRE((sr & SR_REQ) != 0);
    REQUIRE((sr & SR_CD) != 0);   // Command phase: CD=1
    REQUIRE((sr & SR_IO) == 0);   // Command phase: IO=0
    REQUIRE((sr & SR_MSG) == 0);
}

TEST_CASE("ScsiBus status register in DATA_IN phase", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_read_data(std::vector<uint8_t>(256, 0xAA));
    bus.set_target(0, &dev);

    select_target(bus);
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);

    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);
    uint8_t sr = bus.status_register();
    REQUIRE((sr & SR_BSY) != 0);
    REQUIRE((sr & SR_REQ) != 0);
    REQUIRE((sr & SR_CD) == 0);   // Data phase: CD=0
    REQUIRE((sr & SR_IO) != 0);   // Data In: IO=1
    REQUIRE((sr & SR_MSG) == 0);
}

TEST_CASE("ScsiBus status register in DATA_OUT phase", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);
    uint8_t cdb[] = {OP_WRITE_6, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);

    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);
    uint8_t sr = bus.status_register();
    REQUIRE((sr & SR_BSY) != 0);
    REQUIRE((sr & SR_REQ) != 0);
    REQUIRE((sr & SR_CD) == 0);   // Data phase: CD=0
    REQUIRE((sr & SR_IO) == 0);   // Data Out: IO=0
    REQUIRE((sr & SR_MSG) == 0);
}

TEST_CASE("ScsiBus status register in STATUS phase", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);
    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    uint8_t sr = bus.status_register();
    REQUIRE((sr & SR_CD) != 0);   // Status: CD=1
    REQUIRE((sr & SR_IO) != 0);   // Status: IO=1
    REQUIRE((sr & SR_MSG) == 0);  // Status: MSG=0
}

TEST_CASE("ScsiBus status register in MESSAGE_IN phase", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);
    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    bus.read_register(REG_DATA);  // read status

    REQUIRE(bus.phase() == ScsiBusPhase::MessageIn);
    uint8_t sr = bus.status_register();
    REQUIRE((sr & SR_CD) != 0);   // Message: CD=1
    REQUIRE((sr & SR_IO) != 0);   // Message: IO=1
    REQUIRE((sr & SR_MSG) != 0);  // Message: MSG=1
}

// ---------------------------------------------------------------------------
// Complete transaction cycles
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus complete no-data transaction (TEST UNIT READY)", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    // BUS_FREE -> SELECTION -> COMMAND
    select_target(bus);

    // COMMAND -> STATUS (no data phase)
    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::Status);

    // STATUS -> MESSAGE_IN
    uint8_t status = bus.read_register(REG_DATA);
    REQUIRE(status == STATUS_GOOD);
    REQUIRE(bus.phase() == ScsiBusPhase::MessageIn);

    // MESSAGE_IN -> BUS_FREE
    uint8_t message = bus.read_register(REG_DATA);
    REQUIRE(message == MSG_COMMAND_COMPLETE);
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);

    // Verify command was dispatched
    REQUIRE(dev.command_count() == 1);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_TEST_UNIT_READY);
}

TEST_CASE("ScsiBus complete READ(6) transaction", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    std::vector<uint8_t> sector(256);
    for (int i = 0; i < 256; ++i) sector[i] = static_cast<uint8_t>(i);
    dev.set_read_data(sector);
    bus.set_target(0, &dev);

    select_target(bus);

    // READ(6): 1 block from LBA 0
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    // Read all 256 bytes
    auto data = read_all_data(bus);
    REQUIRE(data.size() == 256);
    REQUIRE(data[0] == 0x00);
    REQUIRE(data[255] == 0xFF);

    // Should now be in STATUS
    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    complete_transaction(bus);
}

TEST_CASE("ScsiBus complete WRITE(6) transaction", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    // WRITE(6): 1 block
    uint8_t cdb[] = {OP_WRITE_6, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);

    // Write 256 bytes of data
    for (int i = 0; i < 256; ++i) {
        bus.write_register(REG_DATA, static_cast<uint8_t>(i));
    }

    // Should transition to STATUS after all data written
    REQUIRE(bus.phase() == ScsiBusPhase::Status);

    // Verify command was dispatched with data
    REQUIRE(dev.command_count() == 1);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_WRITE_6);
    REQUIRE(dev.recorded_commands()[0].data_out.size() == 256);
    REQUIRE(dev.recorded_commands()[0].data_out[0] == 0x00);
    REQUIRE(dev.recorded_commands()[0].data_out[255] == 0xFF);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus INQUIRY returns data and completes", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto data = read_all_data(bus);
    REQUIRE(data.size() == 36);
    REQUIRE(data[0] == 0x00);  // Direct access device

    complete_transaction(bus);
}

TEST_CASE("ScsiBus REQUEST SENSE returns sense data", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_sense_data({0x70, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0A});
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_REQUEST_SENSE, 0, 0, 0, 8, 0};
    write_cdb(bus, cdb);

    auto data = read_all_data(bus);
    REQUIRE(data.size() == 8);
    REQUIRE(data[0] == 0x70);
    REQUIRE(data[2] == 0x05);  // Illegal request

    complete_transaction(bus);
}

TEST_CASE("ScsiBus READ CAPACITY returns 8 bytes", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_capacity(40000, 256);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto data = read_all_data(bus);
    REQUIRE(data.size() == 8);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus TRANSLATE returns 4-byte LBA", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_TRANSLATE, 0x01, 0x23, 0x45, 0, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto data = read_all_data(bus);
    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 0x45);  // LSB
    REQUIRE(data[3] == 0x00);  // MSB

    complete_transaction(bus);
}

TEST_CASE("ScsiBus MODE SELECT collects data then executes", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    // MODE SELECT with 22 bytes of parameter data
    uint8_t cdb[] = {OP_MODE_SELECT_6, 0, 0, 0, 22, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);

    for (int i = 0; i < 22; ++i) {
        bus.write_register(REG_DATA, static_cast<uint8_t>(i));
    }

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    REQUIRE(dev.command_count() == 1);
    REQUIRE(dev.recorded_commands()[0].data_out.size() == 22);

    complete_transaction(bus);
}

// ---------------------------------------------------------------------------
// 10-byte CDB handling
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus handles 10-byte CDB (VERIFY)", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_VERIFY_10, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);

    // VERIFY is no-data, should go straight to STATUS
    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    REQUIRE(dev.command_count() == 1);
    REQUIRE(dev.recorded_commands()[0].cdb.size() == 10);

    complete_transaction(bus);
}

// ---------------------------------------------------------------------------
// IRQ handling
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus IRQ not pending by default", "[scsi][bus]") {
    ScsiBus bus;
    REQUIRE_FALSE(bus.irq_pending());
}

TEST_CASE("ScsiBus IRQ enable and assert", "[scsi][bus]") {
    ScsiBus bus;
    bus.write_register(REG_IRQ, 0xFF);
    REQUIRE(bus.irq_pending());
    REQUIRE((bus.status_register() & SR_IRQ) != 0);
}

TEST_CASE("ScsiBus IRQ disable", "[scsi][bus]") {
    ScsiBus bus;
    bus.write_register(REG_IRQ, 0xFF);
    REQUIRE(bus.irq_pending());

    bus.write_register(REG_IRQ, 0x00);
    REQUIRE_FALSE(bus.irq_pending());
    REQUIRE((bus.status_register() & SR_IRQ) == 0);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus target returns CHECK CONDITION", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_default_status(STATUS_CHECK_CONDITION);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    uint8_t status = bus.read_register(REG_DATA);
    REQUIRE(status == STATUS_CHECK_CONDITION);

    // Should still complete normally
    REQUIRE(bus.phase() == ScsiBusPhase::MessageIn);
    bus.read_register(REG_DATA);
    REQUIRE(bus.phase() == ScsiBusPhase::BusFree);
}

// ---------------------------------------------------------------------------
// Multiple transactions
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Multi-block and special-case transfers
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus READ(6) multi-block transfer", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    std::vector<uint8_t> data(512, 0xCC);  // 2 blocks
    dev.set_read_data(data);
    bus.set_target(0, &dev);

    select_target(bus);

    // READ(6): 2 blocks
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 2, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto result = read_all_data(bus);
    REQUIRE(result.size() == 512);
    REQUIRE(result[0] == 0xCC);
    REQUIRE(result[511] == 0xCC);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus READ(6) block count 0 means 256 blocks", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    // 256 blocks * 256 bytes = 65536 bytes
    std::vector<uint8_t> data(65536, 0xDD);
    dev.set_read_data(data);
    bus.set_target(0, &dev);

    select_target(bus);

    // READ(6): block count 0 means 256 blocks
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto result = read_all_data(bus);
    REQUIRE(result.size() == 65536);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus WRITE(6) multi-block transfer", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    // WRITE(6): 2 blocks
    uint8_t cdb[] = {OP_WRITE_6, 0, 0, 0, 2, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);

    for (int i = 0; i < 512; ++i) {
        bus.write_register(REG_DATA, static_cast<uint8_t>(i & 0xFF));
    }

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    REQUIRE(dev.recorded_commands()[0].data_out.size() == 512);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus READ(10) transaction", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    std::vector<uint8_t> data(256, 0xEE);
    dev.set_read_data(data);
    bus.set_target(0, &dev);

    select_target(bus);

    // READ(10): 1 block from LBA 100
    uint8_t cdb[] = {OP_READ_10, 0, 0, 0, 0, 100, 0, 0, 1, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto result = read_all_data(bus);
    REQUIRE(result.size() == 256);

    REQUIRE(dev.recorded_commands()[0].cdb.size() == 10);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_READ_10);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus WRITE(10) transaction", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    // WRITE(10): 1 block
    uint8_t cdb[] = {OP_WRITE_10, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);

    for (int i = 0; i < 256; ++i) {
        bus.write_register(REG_DATA, static_cast<uint8_t>(i));
    }

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_WRITE_10);
    REQUIRE(dev.recorded_commands()[0].data_out.size() == 256);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus WRITE AND VERIFY transaction", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_WRITE_AND_VERIFY, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);

    for (int i = 0; i < 256; ++i) {
        bus.write_register(REG_DATA, static_cast<uint8_t>(i));
    }

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_WRITE_AND_VERIFY);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus MODE SENSE returns data", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    dev.set_mode_sense_data(std::vector<uint8_t>(22, 0x55));
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_MODE_SENSE_6, 0, 0, 0, 22, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto data = read_all_data(bus);
    REQUIRE(data.size() == 22);
    REQUIRE(data[0] == 0x55);

    complete_transaction(bus);
}

TEST_CASE("ScsiBus no-data commands complete to STATUS", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    SECTION("REZERO UNIT") {
        select_target(bus);
        uint8_t cdb[] = {OP_REZERO_UNIT, 0, 0, 0, 0, 0};
        write_cdb(bus, cdb);
        REQUIRE(bus.phase() == ScsiBusPhase::Status);
        complete_transaction(bus);
    }
    SECTION("SEEK") {
        select_target(bus);
        uint8_t cdb[] = {OP_SEEK, 0, 0, 0, 0, 0};
        write_cdb(bus, cdb);
        REQUIRE(bus.phase() == ScsiBusPhase::Status);
        complete_transaction(bus);
    }
    SECTION("START/STOP UNIT") {
        select_target(bus);
        uint8_t cdb[] = {OP_START_STOP_UNIT, 0, 0, 0, 0, 0};
        write_cdb(bus, cdb);
        REQUIRE(bus.phase() == ScsiBusPhase::Status);
        complete_transaction(bus);
    }
    SECTION("FORMAT UNIT") {
        select_target(bus);
        uint8_t cdb[] = {OP_FORMAT_UNIT, 0, 0, 0, 0, 0};
        write_cdb(bus, cdb);
        REQUIRE(bus.phase() == ScsiBusPhase::Status);
        complete_transaction(bus);
    }
}

// ---------------------------------------------------------------------------
// VP415 vendor-specific commands
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus READ F-CODE returns data", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    std::vector<uint8_t> fcode(256, 0);
    fcode[0] = 'N';  // Play command
    fcode[1] = 0x0D;  // CR terminator
    dev.set_read_data(fcode);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_READ_FCODE, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataIn);

    auto data = read_all_data(bus);
    REQUIRE(data.size() == 256);
    REQUIRE(data[0] == 'N');

    complete_transaction(bus);
}

TEST_CASE("ScsiBus WRITE F-CODE accepts data", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    select_target(bus);

    uint8_t cdb[] = {OP_WRITE_FCODE, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb);
    REQUIRE(bus.phase() == ScsiBusPhase::DataOut);

    // Write 256 bytes of F-code data
    for (int i = 0; i < 256; ++i) {
        bus.write_register(REG_DATA, static_cast<uint8_t>(i));
    }

    REQUIRE(bus.phase() == ScsiBusPhase::Status);
    REQUIRE(dev.recorded_commands()[0].cdb[0] == OP_WRITE_FCODE);
    REQUIRE(dev.recorded_commands()[0].data_out.size() == 256);

    complete_transaction(bus);
}

// ---------------------------------------------------------------------------
// Multi-target transactions
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus transactions on different targets", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev0, dev3;
    dev0.set_present(true);
    dev0.set_description("Target 0");
    dev3.set_present(true);
    dev3.set_description("Target 3");
    bus.set_target(0, &dev0);
    bus.set_target(3, &dev3);

    // Transaction on target 0
    select_target(bus, 0);
    uint8_t cdb1[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb1);
    complete_transaction(bus);

    // Transaction on target 3
    select_target(bus, 3);
    uint8_t cdb2[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    write_cdb(bus, cdb2);
    read_all_data(bus);
    complete_transaction(bus);

    REQUIRE(dev0.command_count() == 1);
    REQUIRE(dev0.recorded_commands()[0].cdb[0] == OP_TEST_UNIT_READY);
    REQUIRE(dev3.command_count() == 1);
    REQUIRE(dev3.recorded_commands()[0].cdb[0] == OP_INQUIRY);
}

// ---------------------------------------------------------------------------
// Multiple transactions
// ---------------------------------------------------------------------------

TEST_CASE("ScsiBus multiple transactions on same target", "[scsi][bus]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    // First transaction
    select_target(bus);
    uint8_t cdb1[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    write_cdb(bus, cdb1);
    complete_transaction(bus);

    // Second transaction
    select_target(bus);
    uint8_t cdb2[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    write_cdb(bus, cdb2);
    read_all_data(bus);
    complete_transaction(bus);

    REQUIRE(dev.command_count() == 2);
}
