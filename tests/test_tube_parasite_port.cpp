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

// Tests for TubeParasitePort -- parasite-side adapter for TubeShared.
//
// These tests operate on a single TubeShared instance, using TubeHostPort to
// drive the host side and TubeParasitePort for the parasite side. This mirrors
// the test_tube_host_port.cpp structure but from the parasite's perspective.
//
// Cross-validated against TubeUla's parasite_read/parasite_write where applicable.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeShared.hpp>
#include <beebium/tube/TubeUla.hpp>

using namespace beebium;

// ===========================================================================
// Construction and concept satisfaction
// ===========================================================================

TEST_CASE("TubeParasitePort satisfies TubeParasiteInterface", "[tube][parasite]") {
    // Compile-time check via concept (tested by inclusion of TubeConcepts.hpp).
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);
    // If this compiles, the concept is satisfied.
    (void)parasite;
}

TEST_CASE("TubeParasitePort construction stores shared pointer", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);
    CHECK(parasite.shared() == &shared);
}

// ===========================================================================
// R1: H-to-P latch read (parasite reads what host wrote)
// ===========================================================================

TEST_CASE("TubeParasitePort R1 status shows H-to-P data available after host writes", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Initially no data available from host
    uint8_t status = parasite.parasite_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);

    // Host writes R1 H-to-P
    host.host_write(1, 0x42);

    // Now parasite should see data available
    status = parasite.parasite_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);
}

TEST_CASE("TubeParasitePort R1 data read consumes H-to-P latch", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(1, 0xAB);
    uint8_t data = parasite.parasite_read(1);
    CHECK(data == 0xAB);

    // After reading, ready should be cleared
    CHECK(shared.r1_h2p.ready.load(std::memory_order_acquire) == 0);

    // Status should no longer show data available
    uint8_t status = parasite.parasite_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// R1: P-to-H FIFO write (parasite writes, host reads)
// ===========================================================================

TEST_CASE("TubeParasitePort R1 status shows space available when FIFO not full", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    uint8_t status = parasite.parasite_read(0);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);
}

TEST_CASE("TubeParasitePort R1 data write enqueues to P-to-H FIFO", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    parasite.parasite_write(1, 0x77);

    // Host should see data available
    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Host reads the byte
    CHECK(host.host_read(1) == 0x77);
}

TEST_CASE("TubeParasitePort R1 FIFO fills to 24 bytes", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    for (int i = 0; i < 24; ++i) {
        parasite.parasite_write(1, static_cast<uint8_t>(i));
    }

    // FIFO should be full -- no space available
    uint8_t status = parasite.parasite_read(0);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);

    // Count should be 24
    CHECK(shared.r1_p2h.count.load(std::memory_order_acquire) == 24);
}

TEST_CASE("TubeParasitePort R1 FIFO write when full is silently ignored", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // Fill the FIFO
    for (int i = 0; i < 24; ++i) {
        parasite.parasite_write(1, static_cast<uint8_t>(i));
    }

    // Writing to a full FIFO should be silently ignored
    parasite.parasite_write(1, 0xFF);
    CHECK(shared.r1_p2h.count.load(std::memory_order_acquire) == 24);
}

// ===========================================================================
// R1 status: control flags in bits 5-0
// ===========================================================================

TEST_CASE("TubeParasitePort R1 status includes control flags in bits 5-0", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Set some flags from the host side
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I | TubeUla::FLAG_M);

    uint8_t status = parasite.parasite_read(0);
    CHECK((status & 0x3F) == (TubeUla::FLAG_Q | TubeUla::FLAG_I | TubeUla::FLAG_M));
}

// ===========================================================================
// R2: H-to-P latch (parasite reads)
// ===========================================================================

TEST_CASE("TubeParasitePort R2 status and data read", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Initially no H-to-P data
    uint8_t status = parasite.parasite_read(2);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);

    // Host writes R2 H-to-P
    host.host_write(3, 0xCD);

    // Now data available
    status = parasite.parasite_read(2);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read data
    CHECK(parasite.parasite_read(3) == 0xCD);

    // After reading, no longer available
    status = parasite.parasite_read(2);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// R2: P-to-H latch (parasite writes)
// ===========================================================================

TEST_CASE("TubeParasitePort R2 data write to P-to-H latch", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    parasite.parasite_write(3, 0xEF);

    // Host sees data available
    uint8_t status = host.host_read(2);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Host reads data
    CHECK(host.host_read(3) == 0xEF);
}

TEST_CASE("TubeParasitePort R2 status shows P-to-H space available", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // Initially space available
    uint8_t status = parasite.parasite_read(2);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);

    // Write fills the latch
    parasite.parasite_write(3, 0x42);

    // No space until host reads
    status = parasite.parasite_read(2);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

// ===========================================================================
// R3: H-to-P register (parasite reads)
// ===========================================================================

TEST_CASE("TubeParasitePort R3 1-byte mode: H-to-P read", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Default is 1-byte mode (V=0). Host writes one byte.
    host.host_write(5, 0xAA);

    // Parasite should see data available
    uint8_t status = parasite.parasite_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read the byte
    CHECK(parasite.parasite_read(5) == 0xAA);

    // After reading, no longer available
    status = parasite.parasite_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

TEST_CASE("TubeParasitePort R3 2-byte mode: H-to-P read", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Set V=1 for 2-byte mode
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);

    // Host writes two bytes
    host.host_write(5, 0x11);
    host.host_write(5, 0x22);

    // Data available now that both bytes are written
    uint8_t status = parasite.parasite_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read both bytes in order
    CHECK(parasite.parasite_read(5) == 0x11);
    CHECK(parasite.parasite_read(5) == 0x22);
}

// ===========================================================================
// R3: P-to-H register (parasite writes)
// ===========================================================================

TEST_CASE("TubeParasitePort R3 P-to-H write in 1-byte mode", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Clear the dummy byte from init
    host.host_read(5);  // consume dummy byte
    // Verify P-to-H pending is cleared
    CHECK(shared.r3_p2h.pending.load(std::memory_order_acquire) == 0);

    // Parasite writes one byte (V=0, threshold=1)
    parasite.parasite_write(5, 0xBB);

    // Host should see data available
    uint8_t status = host.host_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    CHECK(host.host_read(5) == 0xBB);
}

TEST_CASE("TubeParasitePort R3 P-to-H write in 2-byte mode", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Set V=1 for 2-byte mode
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);

    // Clear the dummy byte from init
    host.host_read(5);

    // Parasite writes two bytes
    parasite.parasite_write(5, 0xCC);
    parasite.parasite_write(5, 0xDD);

    // Both should be available
    CHECK(host.host_read(5) == 0xCC);
    CHECK(host.host_read(5) == 0xDD);
}

TEST_CASE("TubeParasitePort R3 status shows P-to-H space available", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // After init(), R3 P-to-H count=0, pending=0 -- space available
    uint8_t status = parasite.parasite_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);

    // After reset(), R3 P-to-H has dummy byte (pending=1) -- no space
    parasite.reset();
    status = parasite.parasite_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);

    // Clear the dummy byte
    shared.r3_p2h.count.store(0, std::memory_order_relaxed);
    shared.r3_p2h.pending.store(0, std::memory_order_release);

    // Space available again
    status = parasite.parasite_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);
}

// ===========================================================================
// R3 status: N flag (bit 5) -- parasite-specific
// ===========================================================================

TEST_CASE("TubeParasitePort R3 status N flag set when data or space available", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // After reset(), R3 P-to-H has dummy byte (pending=1, no space),
    // and no H-to-P data.
    // N = data_available OR space_available = false OR false = false
    parasite.reset();
    uint8_t status = parasite.parasite_read(4);
    CHECK((status & 0x20) == 0);  // N flag clear

    // Clear dummy byte to make space available
    shared.r3_p2h.count.store(0, std::memory_order_relaxed);
    shared.r3_p2h.pending.store(0, std::memory_order_release);

    status = parasite.parasite_read(4);
    CHECK((status & 0x20) != 0);  // N flag set (space available)

    // Write H-to-P data from host
    host.host_write(5, 0x42);

    status = parasite.parasite_read(4);
    CHECK((status & 0x20) != 0);  // N flag still set (data available)
}

TEST_CASE("TubeParasitePort R3 status bits 4-0 read as 1", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    uint8_t status = parasite.parasite_read(4);
    CHECK((status & 0x1F) == 0x1F);
}

// ===========================================================================
// R4: H-to-P latch (parasite reads)
// ===========================================================================

TEST_CASE("TubeParasitePort R4 status and data read", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Initially no H-to-P data
    uint8_t status = parasite.parasite_read(6);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);

    // Host writes R4 H-to-P
    host.host_write(7, 0x99);

    // Data available
    status = parasite.parasite_read(6);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read data
    CHECK(parasite.parasite_read(7) == 0x99);

    // After reading, not available
    status = parasite.parasite_read(6);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// R4: P-to-H latch (parasite writes)
// ===========================================================================

TEST_CASE("TubeParasitePort R4 data write to P-to-H latch", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    parasite.parasite_write(7, 0x55);

    // Host sees data
    CHECK((host.host_read(6) & TubeUla::DATA_AVAILABLE) != 0);
    CHECK(host.host_read(7) == 0x55);
}

TEST_CASE("TubeParasitePort R4 status shows P-to-H space available", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // Initially space available
    uint8_t status = parasite.parasite_read(6);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);

    // Write fills the latch
    parasite.parasite_write(7, 0x42);

    // No space until host reads
    status = parasite.parasite_read(6);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

// ===========================================================================
// Parasite cannot write to control register
// ===========================================================================

TEST_CASE("TubeParasitePort writing to offset 0 has no effect", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // Set some flags via shared directly
    shared.control_flags.store(TubeUla::FLAG_Q, std::memory_order_release);

    // Parasite tries to write to control register
    parasite.parasite_write(0, 0xFF);

    // Flags should be unchanged
    CHECK(shared.control_flags.load(std::memory_order_acquire) == TubeUla::FLAG_Q);
}

// ===========================================================================
// Status registers: no writable effect from parasite at even offsets 2, 4, 6
// ===========================================================================

TEST_CASE("TubeParasitePort writing to status registers has no effect", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // These should be no-ops
    parasite.parasite_write(2, 0xFF);
    parasite.parasite_write(4, 0xFF);
    parasite.parasite_write(6, 0xFF);

    // Verify no side effects on any registers
    CHECK(shared.r2_p2h.ready.load(std::memory_order_acquire) == 0);
    CHECK(shared.r4_p2h.ready.load(std::memory_order_acquire) == 0);
}

// ===========================================================================
// PIRQ: parasite IRQ
// ===========================================================================

TEST_CASE("TubeParasitePort PIRQ deasserted when I=0 and J=0", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Write data to R1 H-to-P and R4 H-to-P -- but no enable flags
    host.host_write(1, 0x42);
    host.host_write(7, 0x43);

    CHECK_FALSE(parasite.pirq());
}

TEST_CASE("TubeParasitePort PIRQ asserted when I=1 and R1 H-to-P has data", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Enable I flag
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);

    // No data yet -- no PIRQ
    CHECK_FALSE(parasite.pirq());

    // Host writes R1 H-to-P
    host.host_write(1, 0x42);

    CHECK(parasite.pirq());
}

TEST_CASE("TubeParasitePort PIRQ deasserted after parasite reads R1 data", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
    host.host_write(1, 0x42);
    REQUIRE(parasite.pirq());

    // Parasite reads R1 data -- clears ready
    parasite.parasite_read(1);

    CHECK_FALSE(parasite.pirq());
}

TEST_CASE("TubeParasitePort PIRQ asserted when J=1 and R4 H-to-P has data", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);

    CHECK_FALSE(parasite.pirq());

    host.host_write(7, 0x99);

    CHECK(parasite.pirq());
}

TEST_CASE("TubeParasitePort PIRQ deasserted after parasite reads R4 data", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);
    host.host_write(7, 0x99);
    REQUIRE(parasite.pirq());

    parasite.parasite_read(7);

    CHECK_FALSE(parasite.pirq());
}

TEST_CASE("TubeParasitePort PIRQ from both R1 and R4 simultaneously", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I | TubeUla::FLAG_J);
    host.host_write(1, 0x42);
    host.host_write(7, 0x99);

    CHECK(parasite.pirq());

    // Consuming R1 alone leaves R4 still asserting
    parasite.parasite_read(1);
    CHECK(parasite.pirq());

    // Consuming R4 clears it
    parasite.parasite_read(7);
    CHECK_FALSE(parasite.pirq());
}

// ===========================================================================
// PNMI: parasite NMI (edge-triggered from R3 when M=1)
// ===========================================================================

TEST_CASE("TubeParasitePort PNMI not asserted when M=0", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Write H-to-P data to R3 without M flag
    host.host_write(5, 0xAA);

    CHECK_FALSE(parasite.pnmi());
}

TEST_CASE("TubeParasitePort PNMI asserted on rising edge when H-to-P data arrives", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // After reset(), R3 P-to-H has dummy byte (pending=1),
    // so space is not available. With no H-to-P data, PNMI level = false.
    parasite.reset();

    // Enable M flag
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Read status to establish prev_pnmi baseline (PNMI level = false)
    parasite.parasite_read(4);
    CHECK_FALSE(parasite.pnmi());

    // Host writes H-to-P -- makes data available, raising PNMI level.
    // Parasite must do a register access to detect the edge.
    host.host_write(5, 0xAA);
    parasite.parasite_read(4);  // triggers update_pnmi

    CHECK(parasite.pnmi());
}

TEST_CASE("TubeParasitePort PNMI clears when condition removed", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // After reset(), R3 P-to-H has dummy byte (pending=1, no space).
    parasite.reset();

    // Enable M flag
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Establish baseline (PNMI level = false: no H-to-P data, no P-to-H space)
    parasite.parasite_read(4);
    CHECK_FALSE(parasite.pnmi());

    // Host writes H-to-P data -- raises PNMI level
    host.host_write(5, 0xAA);
    parasite.parasite_read(4);  // triggers edge detection
    CHECK(parasite.pnmi());

    // Parasite reads the R3 data -- removes data_available condition.
    // P-to-H is still pending (dummy byte), so space_available = false.
    // Both conditions false -> PNMI level drops -> edge clears.
    parasite.parasite_read(5);
    CHECK_FALSE(parasite.pnmi());
}

TEST_CASE("TubeParasitePort PNMI edge: space available triggers NMI", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // After reset(), P-to-H has dummy byte (pending=1, no space), no H-to-P data.
    parasite.reset();

    // Enable M flag
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Establish baseline (PNMI level = false)
    parasite.parasite_read(4);
    CHECK_FALSE(parasite.pnmi());

    // Host reads the dummy byte -- clears pending, making P-to-H space available
    host.host_read(5);

    // Parasite must do a register access to detect the edge
    parasite.parasite_read(4);
    CHECK(parasite.pnmi());
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_CASE("TubeParasitePort reset clears parasite-owned registers", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // Write some data to parasite-owned registers
    parasite.parasite_write(1, 0x42);  // R1 P-to-H
    parasite.parasite_write(3, 0x43);  // R2 P-to-H
    parasite.parasite_write(7, 0x44);  // R4 P-to-H

    parasite.reset();

    // P-to-H registers should be cleared
    CHECK(shared.r1_p2h.count.load(std::memory_order_acquire) == 0);
    CHECK(shared.r2_p2h.ready.load(std::memory_order_acquire) == 0);
    CHECK(shared.r4_p2h.ready.load(std::memory_order_acquire) == 0);

    // R3 P-to-H should have dummy byte (like soft_reset)
    CHECK(shared.r3_p2h.count.load(std::memory_order_acquire) == 1);
    CHECK(shared.r3_p2h.pending.load(std::memory_order_acquire) == 1);
}

TEST_CASE("TubeParasitePort reset clears H-to-P registers", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Host writes some data
    host.host_write(1, 0x42);
    host.host_write(3, 0x43);
    host.host_write(7, 0x44);

    parasite.reset();

    // H-to-P registers should be cleared
    CHECK(shared.r1_h2p.ready.load(std::memory_order_acquire) == 0);
    CHECK(shared.r2_h2p.ready.load(std::memory_order_acquire) == 0);
    CHECK(shared.r4_h2p.ready.load(std::memory_order_acquire) == 0);
    CHECK(shared.r3_h2p.count.load(std::memory_order_acquire) == 0);
    CHECK(shared.r3_h2p.pending.load(std::memory_order_acquire) == 0);
}

TEST_CASE("TubeParasitePort reset clears interrupt state", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Set up conditions for PIRQ
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
    host.host_write(1, 0x42);
    REQUIRE(parasite.pirq());

    parasite.reset();

    CHECK_FALSE(parasite.pirq());
    CHECK_FALSE(parasite.pnmi());
}

// ===========================================================================
// Address mirroring (offset & 7)
// ===========================================================================

TEST_CASE("TubeParasitePort address mirroring", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // Offset 8 should mirror to offset 0 (R1 status)
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    uint8_t status_0 = parasite.parasite_read(0);
    uint8_t status_8 = parasite.parasite_read(8);
    CHECK((status_0 & 0x3F) == (status_8 & 0x3F));
}

// ===========================================================================
// Cross-validation against TubeUla
// ===========================================================================

TEST_CASE("TubeParasitePort R1 status matches TubeUla parasite_read", "[tube][parasite][cross-validation]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    TubeUla ula;

    // Set same control flags
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);
    ula.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);

    // Write same data
    host.host_write(1, 0x42);
    ula.host_write(1, 0x42);

    uint8_t shared_status = parasite.parasite_read(0);
    uint8_t ula_status = ula.parasite_read(0);

    CHECK(shared_status == ula_status);
}

TEST_CASE("TubeParasitePort R2 round-trip matches TubeUla", "[tube][parasite][cross-validation]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    TubeUla ula;

    // Host writes R2
    host.host_write(3, 0xAB);
    ula.host_write(3, 0xAB);

    // Parasite reads R2
    CHECK(parasite.parasite_read(3) == ula.parasite_read(3));
}

TEST_CASE("TubeParasitePort R4 round-trip matches TubeUla", "[tube][parasite][cross-validation]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    TubeUla ula;

    // Host writes R4
    host.host_write(7, 0xCD);
    ula.host_write(7, 0xCD);

    // Parasite reads R4
    CHECK(parasite.parasite_read(7) == ula.parasite_read(7));

    // Status should match too
    CHECK(parasite.parasite_read(6) == ula.parasite_read(6));
}

TEST_CASE("TubeParasitePort R3 status matches TubeUla parasite_read", "[tube][parasite][cross-validation]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // TubeUla constructor calls reset() which adds the dummy byte.
    // Match that state on the shared side.
    parasite.reset();

    TubeUla ula;

    // R3 status including N flag (bit 5) and bits 4-0
    uint8_t shared_status = parasite.parasite_read(4);
    uint8_t ula_status = ula.parasite_read(4);

    CHECK(shared_status == ula_status);
}

TEST_CASE("TubeParasitePort PIRQ matches TubeUla", "[tube][parasite][cross-validation]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    TubeUla ula;

    // Set I flag and write R1 data on both
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
    ula.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);

    CHECK(parasite.pirq() == ula.pirq());  // both false

    host.host_write(1, 0x42);
    ula.host_write(1, 0x42);

    CHECK(parasite.pirq() == ula.pirq());  // both true
}
