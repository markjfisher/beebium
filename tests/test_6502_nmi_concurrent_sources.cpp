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

// Tests for the 6502 library's NMI edge detection with multiple concurrent
// NMI sources.
//
// On the BBC Micro, NMI can come from several devices: the disc controller
// (WD1770 / 8271 INTRQ), the Econet ADLC (gated through the INTON/INTOFF
// flip-flop), and potentially others. On real hardware these are wired-OR
// onto a single active-low NMI pin through open-collector drivers (see
// the Econet / disc NMI gating circuitry in BBC Service Manual Appendix J).
//
// The 6502 library models each source as a separate bit in
// M6502::device_nmi_flags. An edge is recorded in M6502::nmi_flags when a
// source transitions from 0 to 1 via M6502_SetDeviceNMI. The interrupt
// entry sequence (Cycle4_Interrupt for NMOS, Cycle0_InterruptCMOS for
// CMOS) clears nmi_flags when vectoring to 0xFFFA but does NOT touch
// device_nmi_flags.
//
// These tests verify that the per-source edge detection matches the
// wired-OR behaviour of real hardware:
//
//   1. A source toggling 0->1 records an edge.
//   2. An edge stays recorded in nmi_flags until the interrupt is entered
//      (or the test harness clears it).
//   3. When multiple sources are active concurrently, nmi_flags
//      accumulates edges from each.
//   4. After interrupt entry, if a source is STILL asserted (never went
//      back to 0), no new edge is generated on further SetDeviceNMI(1)
//      calls -- matching the real NMI pin, which stays low and produces
//      no new falling edge.
//   5. Only when a source goes 1->0 and then back to 0->1 does a fresh
//      edge fire -- on the BBC Micro this is how the Econet INTOFF/INTON
//      mechanism re-arms the NMI while the ADLC IRQ remains asserted.

#include <catch2/catch_test_macros.hpp>
#include <6502/6502.h>

#include <cstdint>

namespace {

constexpr uint8_t kDiscMask   = 0x01;
constexpr uint8_t kEconetMask = 0x02;

M6502 make_cpu() {
    M6502 cpu{};
    M6502_Init(&cpu, &M6502_nmos6502_config);
    return cpu;
}

} // namespace

TEST_CASE("NMI edge detection: single source raise records an edge",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    CHECK((cpu.nmi_flags & kDiscMask) != 0);
    CHECK((cpu.device_nmi_flags & kDiscMask) != 0);

    M6502_Destroy(&cpu);
}

TEST_CASE("NMI edge detection: source lowering clears its device bit "
          "but not nmi_flags",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    REQUIRE((cpu.nmi_flags & kDiscMask) != 0);

    // Source de-asserts. nmi_flags is latched state of an edge that has
    // already been recorded; de-asserting the source does not undo the
    // edge. device_nmi_flags reflects the current line state.
    M6502_SetDeviceNMI(&cpu, kDiscMask, 0);
    CHECK((cpu.device_nmi_flags & kDiscMask) == 0);
    CHECK((cpu.nmi_flags & kDiscMask) != 0);

    M6502_Destroy(&cpu);
}

TEST_CASE("NMI edge detection: re-assertion after de-assertion records a "
          "new edge",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    // Full cycle: assert -> enter interrupt (clears nmi_flags) ->
    // handler de-asserts -> handler re-asserts (e.g. INTOFF then INTON
    // on the BBC Econet path).
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    cpu.nmi_flags = 0;                            // simulate T4_Interrupt
    M6502_SetDeviceNMI(&cpu, kEconetMask, 0);
    REQUIRE(cpu.device_nmi_flags == 0);

    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK((cpu.nmi_flags & kEconetMask) != 0);

    M6502_Destroy(&cpu);
}

TEST_CASE("NMI edge detection: concurrent sources each record an edge",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    REQUIRE(cpu.nmi_flags == kDiscMask);

    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK(cpu.nmi_flags == (kDiscMask | kEconetMask));
    CHECK(cpu.device_nmi_flags == (kDiscMask | kEconetMask));

    M6502_Destroy(&cpu);
}

// Hardware-fidelity test: the 6502 library's per-source edge detection
// correctly models a wired-OR NMI pin. Once the CPU has vectored through
// an interrupt entry that consumed the edge, any source that was (and
// remains) continuously asserted cannot on its own produce a new edge.
// On real hardware this corresponds to the NMI pin staying low because
// one of the OR'd sources is still pulling it down, leaving no new
// falling edge for the 6502 to detect.
//
// For the BBC Econet this is why the NFS NMI handler's INTOFF/INTON
// sequence is necessary: the Econet side of the OR must be released and
// re-asserted to generate a new edge while the ADLC's IRQ output
// remains high. The disc NMI handler on its own does not toggle the
// Econet flip-flop, so a still-asserted Econet source will not re-fire
// solely from the passage of time.
TEST_CASE("NMI edge detection: persistently-asserted source does not "
          "re-fire after interrupt entry consumes its edge",
          "[6502][nmi]") {
    M6502 cpu = make_cpu();
    cpu.nmi_flags = 0;
    cpu.device_nmi_flags = 0;

    // Both sources fire; both edges latched in nmi_flags.
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    M6502_SetDeviceNMI(&cpu, kDiscMask, 1);
    REQUIRE(cpu.nmi_flags == (kDiscMask | kEconetMask));

    // CPU enters interrupt; entry sequence clears nmi_flags but not
    // device_nmi_flags.
    cpu.nmi_flags = 0;
    REQUIRE(cpu.device_nmi_flags == (kDiscMask | kEconetMask));

    // The serviced handler (e.g. the disc handler) de-asserts only its
    // own source.
    M6502_SetDeviceNMI(&cpu, kDiscMask, 0);
    REQUIRE(cpu.device_nmi_flags == kEconetMask);

    // The other source remains asserted. Further SetDeviceNMI(1) calls
    // do NOT produce a new edge, because the device bit never went back
    // to 0 first.
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK(cpu.nmi_flags == 0);

    // Confirm the edge is re-armed only by a round trip through 0.
    M6502_SetDeviceNMI(&cpu, kEconetMask, 0);
    M6502_SetDeviceNMI(&cpu, kEconetMask, 1);
    CHECK((cpu.nmi_flags & kEconetMask) != 0);

    M6502_Destroy(&cpu);
}
