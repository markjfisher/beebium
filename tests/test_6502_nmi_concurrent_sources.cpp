// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Tests for 6502 NMI edge detection with concurrent NMI sources.
//
// On the BBC Micro, NMI can come from several devices: the disc controller
// (bit 0), the Econet ADLC (bit 1), and potentially others. Each source is
// assigned a bit in M6502::device_nmi_flags, and new edges are recorded in
// M6502::nmi_flags.
//
// The M6502 interrupt entry (Cycle4_Interrupt for NMOS, Cycle0_InterruptCMOS
// for CMOS) clears nmi_flags when vectoring to 0xFFFA. It does NOT touch
// device_nmi_flags. This could cause "lost edges" when a second NMI source
// is active concurrent with the first: once the first is handled and
// nmi_flags is cleared, the second source's bit is stuck set in
// device_nmi_flags with no way to generate a new edge.
//
// These tests exercise that scenario directly to confirm or refute the
// hypothesis that the BBC Micro's Econet NMI can be "lost" when the disc
// controller also has an NMI pending.

#include <catch2/catch_test_macros.hpp>
#include <6502/6502.h>

#include <cstdint>

namespace {

constexpr uint8_t kDiscMask   = 0x01;
constexpr uint8_t kEconetMask = 0x02;

// Minimal harness: the 6502 object only needs its internal flags manipulated
// via M6502_SetDeviceNMI. We don't need to run cycles -- the edge detection
// happens entirely in M6502_SetDeviceNMI.
M6502 make_cpu() {
    M6502 cpu{};
    M6502_Init(&cpu, &M6502_nmos6502_config);
    return cpu;
}

} // namespace

TEST_CASE("NMI edge detection: single source raise and lower",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    // Assert disc NMI -- edge should be recorded
    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    CHECK((cpu.nmi_flags & kDiscMask) != 0);
    CHECK((cpu.device_nmi_flags & kDiscMask) != 0);

    // Simulate interrupt entry: nmi_flags cleared by T4_Interrupt /
    // Cycle0_InterruptCMOS
    cpu.nmi_flags = 0;

    // Disc NMI handler de-asserts the line (e.g. by reading the disc status)
    M6502_SetDeviceNMI(&cpu, kDiscMask, 0);
    CHECK((cpu.device_nmi_flags & kDiscMask) == 0);
    CHECK(cpu.nmi_flags == 0);

    // Disc NMI re-asserts for a new interrupt
    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    CHECK((cpu.nmi_flags & kDiscMask) != 0);   // Edge re-detected

    M6502_Destroy(&cpu);
}

TEST_CASE("NMI edge detection: second source asserts before first clears",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    // Disc NMI fires first
    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    CHECK(cpu.nmi_flags == kDiscMask);

    // Econet NMI fires while disc is still asserted
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK(cpu.nmi_flags == (kDiscMask | kEconetMask));
    CHECK(cpu.device_nmi_flags == (kDiscMask | kEconetMask));

    M6502_Destroy(&cpu);
}

// THE CRITICAL TEST for the Bug 2 hypothesis.
//
// Scenario:
//   1. Econet NMI asserts (bit 1).
//   2. Disc NMI asserts (bit 0) shortly after -- both edges now in nmi_flags.
//   3. The 6502 enters its interrupt sequence. T4_Interrupt clears
//      nmi_flags (both bits), but device_nmi_flags keeps both bits.
//   4. The NMI handler is the disc handler (not Econet). It reads the disc
//      status, which de-asserts the disc NMI line -- SetDeviceNMI clears
//      the disc bit in device_nmi_flags.
//   5. The disc handler does RTI without ever toggling the Econet NMI
//      enable flip-flop.
//   6. The Econet source is still active (ADLC IRQ still high).
//      SetDeviceNMI(econet, 1) is called every cycle -- but the Econet bit
//      is already set in device_nmi_flags, so no new edge is recorded.
//
// If this test fails (nmi_flags is still 0 after step 6), the Econet NMI
// is "lost" and will never fire until something toggles the Econet
// device bit 0->0 and back to 0->1. On the BBC this toggle comes from
// INTOFF/INTON (the station ID register read and the Video ULA write),
// which only runs inside the NMI handler -- and the NMI handler isn't
// running because of this very lockout.
//
// If this test PASSES (nmi_flags gets the Econet bit), the hypothesis is
// wrong and we need to look elsewhere.
TEST_CASE("NMI edge detection: Econet edge lost when disc NMI handler "
          "runs first and Econet source remains asserted",
          "[6502][nmi][regression]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    // Step 1: Econet NMI fires (scout ack byte entered FIFO).
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK((cpu.nmi_flags & kEconetMask) != 0);

    // Step 2: Disc NMI fires concurrently (e.g. WD1770 DRQ during disc I/O).
    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    CHECK(cpu.nmi_flags == (kDiscMask | kEconetMask));

    // Step 3: 6502 enters interrupt sequence. T4_Interrupt /
    // Cycle0_InterruptCMOS clears nmi_flags.
    cpu.nmi_flags = 0;
    // device_nmi_flags still has both bits.
    REQUIRE(cpu.device_nmi_flags == (kDiscMask | kEconetMask));

    // Step 4: Disc NMI handler runs. It reads the disc status, which
    // de-asserts the disc NMI line.
    M6502_SetDeviceNMI(&cpu, kDiscMask, 0);
    CHECK(cpu.device_nmi_flags == kEconetMask);  // Only Econet bit left

    // Step 5 (implied): disc handler RTIs without ever toggling Econet.

    // Step 6: Econet source is still active (ADLC IRQ still high, bytes
    // still in FIFO). Every subsequent cycle, Machine::step calls
    // SetDeviceNMI(econet, 1). But the bit is already set in
    // device_nmi_flags...
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);

    // If nmi_flags has the Econet bit here, the CPU will recognise the
    // pending NMI and service the Econet handler. The test PASSES.
    //
    // If nmi_flags is still 0, the Econet edge is lost. The CPU will not
    // service any further NMI from the Econet source until the Econet
    // device bit toggles to 0 and back to 1. The test FAILS, confirming
    // the Bug 2 lost-edge hypothesis.
    INFO("nmi_flags=0x" << std::hex << (int)cpu.nmi_flags);
    INFO("device_nmi_flags=0x" << std::hex << (int)cpu.device_nmi_flags);
    CHECK((cpu.nmi_flags & kEconetMask) != 0);

    M6502_Destroy(&cpu);
}

// Positive control: if the Econet source BRIEFLY de-asserts (e.g. because
// the NFS ROM's NMI handler runs INTOFF), a subsequent re-assertion DOES
// produce a new edge. This confirms the edge detection works correctly
// when the source bit transitions through 0.
TEST_CASE("NMI edge detection: re-assertion after de-assertion produces "
          "a new edge", "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    // Assert, interrupt entry, handler INTOFFs (source de-asserts)
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    cpu.nmi_flags = 0;
    M6502_SetDeviceNMI(&cpu, kEconetMask, 0);
    REQUIRE(cpu.device_nmi_flags == 0);

    // INTON (source re-asserts)
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK((cpu.nmi_flags & kEconetMask) != 0);   // Edge re-detected

    M6502_Destroy(&cpu);
}
