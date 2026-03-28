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

// Narrow protocol tests that simulate the exact register access sequences
// that ADFS 1.30's SCSI driver performs, without booting the full machine.
// These run in microseconds and pinpoint protocol compatibility issues.

#include <catch2/catch_test_macros.hpp>
#include <ScsiBus.hpp>
#include <ScsiTestDevice.hpp>
#include <ScsiConstants.hpp>

using namespace beebium;
using namespace beebium::scsi;

// Simulate ADFS register access: write_register maps to the 4 offsets
// exactly as the 6502 sees them at 0xFC40-0xFC43.
// REG_DATA=0, REG_STATUS=1, REG_SELECT=2, REG_IRQ=3

TEST_CASE("ADFS probe: write 0x5A and read back without target", "[scsi][adfs-protocol]") {
    ScsiBus bus;
    // No targets installed -- probe should not enter Selection

    // ADFS writes test pattern to data register
    bus.write_register(REG_DATA, 0x5A);  // Write0: sel=true

    // Without a target, we should stay in BusFree
    // (enter_selection finds no present target)
    CHECK(bus.phase() == ScsiBusPhase::BusFree);

    // Read back should return 0x5A
    uint8_t readback = bus.read_register(REG_DATA);
    CHECK(readback == 0x5A);
}

TEST_CASE("ADFS probe: write 0x5A and read back with target", "[scsi][adfs-protocol]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    // ADFS writes test pattern -- should NOT enter Selection (probe only)
    bus.write_register(REG_DATA, 0x5A);  // Write0: sel=true
    CHECK(bus.phase() == ScsiBusPhase::BusFree);

    // Read back should return 0x5A
    uint8_t readback = bus.read_register(REG_DATA);
    CHECK(readback == 0x5A);
}

TEST_CASE("ADFS probe: full test pattern sequence from trace", "[scsi][adfs-protocol]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    // Replaying the exact sequence from the ADFS boot trace:

    // 1. W reg=0 val=0x5A → sel=true, stays in BusFree (probe)
    bus.write_register(REG_DATA, 0x5A);
    CHECK(bus.phase() == ScsiBusPhase::BusFree);

    // 2. W reg=3 val=0x00 → IRQ disable
    bus.write_register(REG_IRQ, 0x00);
    CHECK(bus.phase() == ScsiBusPhase::BusFree);  // still in BusFree

    // 3. R reg=0 → should return 0x5A
    CHECK(bus.read_register(REG_DATA) == 0x5A);

    // 4. W reg=0 val=0xA5 → second test pattern
    bus.write_register(REG_DATA, 0xA5);
    CHECK(bus.phase() == ScsiBusPhase::BusFree);

    // 5. W reg=3 val=0x00 → IRQ disable
    bus.write_register(REG_IRQ, 0x00);

    // 6. R reg=0 → should return 0xA5
    CHECK(bus.read_register(REG_DATA) == 0xA5);

    // Now ADFS is convinced hardware is present.
    // 7-8: Repeats of the test pattern (from trace)
    bus.write_register(REG_DATA, 0x5A);
    bus.write_register(REG_IRQ, 0x00);
    CHECK(bus.read_register(REG_DATA) == 0x5A);

    bus.write_register(REG_DATA, 0xA5);
    bus.write_register(REG_IRQ, 0x00);
    CHECK(bus.read_register(REG_DATA) == 0xA5);

    // After the probe, ADFS reads the status register
    uint8_t sr = bus.read_register(REG_STATUS);
    INFO("Status register after probe: 0x" << std::hex << (int)sr);
    CHECK((sr & SR_BSY) == 0);  // Still in BusFree, no BSY
    CHECK((sr & SR_REQ) != 0);  // REQ always set (Acorn adapter quirk)

    // ADFS then initiates a real selection by writing target ID and reg 2
    bus.write_register(REG_DATA, 0x01);    // target 0
    bus.write_register(REG_SELECT, 0x00);  // trigger Selection → Command
    CHECK(bus.phase() == ScsiBusPhase::Command);
}

TEST_CASE("ADFS probe: what happens if we DON'T enter Selection on probe writes", "[scsi][adfs-protocol]") {
    // Hypothesis: the probe writes should NOT trigger Selection.
    // On real hardware, Selection requires both the host and target to
    // assert their IDs on the bus. The probe writes 0x5A which has
    // multiple bits set -- not a valid single-target selection.
    // Maybe we should only enter Selection when the value has exactly
    // one bit set (a valid SCSI target ID)?

    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    // Write 0x5A (multiple bits set -- probe pattern, not selection)
    bus.write_register(REG_DATA, 0x5A);
    // If we DON'T enter Selection, status register shows BusFree
    INFO("Phase after 0x5A write: " << std::string(scsi_phase_name(bus.phase())));

    // Write 0x01 (single bit -- valid target 0 selection)
    // This SHOULD trigger selection
    bus.write_register(REG_DATA, 0x01);
    INFO("Phase after 0x01 write: " << std::string(scsi_phase_name(bus.phase())));
}

TEST_CASE("Compare: b2-style selection requires Write2 to enter Command", "[scsi][adfs-protocol]") {
    ScsiBus bus;
    ScsiTestDevice dev;
    dev.set_present(true);
    bus.set_target(0, &dev);

    // Simulate a proper SCSI selection sequence:
    // 1. Write target ID to data register (stays in BusFree)
    bus.write_register(REG_DATA, 0x01);  // target 0
    CHECK(bus.phase() == ScsiBusPhase::BusFree);

    // 2. Write to select register (triggers Selection → Command)
    bus.write_register(REG_SELECT, 0x00);
    CHECK(bus.phase() == ScsiBusPhase::Command);

    // 3. Write TEST UNIT READY CDB
    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    for (auto byte : cdb) {
        bus.write_register(REG_DATA, byte);
    }
    CHECK(bus.phase() == ScsiBusPhase::Status);

    // 4. Read status
    uint8_t status = bus.read_register(REG_DATA);
    CHECK(status == STATUS_GOOD);

    // 5. Read message
    bus.read_register(REG_DATA);
    CHECK(bus.phase() == ScsiBusPhase::BusFree);
}
