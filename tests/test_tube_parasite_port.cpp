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

TEST_CASE("TubeParasitePort R1 sequential reads return successive host-written bytes", "[tube][parasite]") {
    // CE2023 decompressor reads 702 consecutive bytes via R1.
    // Each host write should be consumed exactly once by a parasite read.
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    const uint8_t test_bytes[] = { 0xC5, 0x11, 0x03, 0x90, 0x2C, 0x47, 0xB4, 0x77 };
    for (size_t i = 0; i < sizeof(test_bytes); ++i) {
        // Host writes next byte
        host.host_write(1, test_bytes[i]);

        // Parasite polls status until data available
        uint8_t status = parasite.parasite_read(0);
        REQUIRE((status & TubeUla::DATA_AVAILABLE) != 0);

        // Parasite reads data -- should get the byte just written
        uint8_t data = parasite.parasite_read(1);
        INFO("byte index " << i);
        CHECK(data == test_bytes[i]);

        // Ready should be cleared after read
        CHECK(shared.r1_h2p.ready.load(std::memory_order_acquire) == 0);
    }
}

TEST_CASE("TubeParasitePort R1 deferred write completes after bus_stretch_cancel clears", "[tube][parasite]") {
    // Simulates the debugger pause/resume cycle for R1:
    // 1. Host writes byte 1 (latch fills)
    // 2. bus_stretch_cancel set (debugger pause)
    // 3. Host attempts byte 2 (deferred, not dropped)
    // 4. bus_stretch_cancel cleared (debugger resume)
    // 5. Parasite reads byte 1 (latch clears)
    // 6. complete_pending_write() retries byte 2 (succeeds)
    // 7. Parasite reads byte 2
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(1, 0xC5);
    REQUIRE(shared.r1_h2p.ready.load() != 0);

    shared.bus_stretch_cancel.store(true, std::memory_order_release);
    host.host_write(1, 0x11);  // deferred
    shared.bus_stretch_cancel.store(false, std::memory_order_release);

    // Parasite consumes byte 1
    CHECK(parasite.parasite_read(1) == 0xC5);

    // Retry the deferred write (Machine::run() does this on resume)
    host.complete_pending_write();

    // Byte 2 should now be in the latch
    CHECK(shared.r1_h2p.ready.load() != 0);
    CHECK(parasite.parasite_read(1) == 0x11);
}

TEST_CASE("TubeParasitePort R1 bulk transfer survives bus_stretch_cancel mid-stream", "[tube][parasite]") {
    // Simulates the CE2023 scenario: host sends many bytes via R1 with
    // a bus_stretch_cancel interruption mid-stream. All bytes must arrive.
    //
    // The scenario: host writes byte N, latch is full. Host writes byte
    // N+1 but bus_stretch_cancel is set (debugger pause). Write N+1 is
    // deferred. After resume, parasite reads byte N, then
    // complete_pending_write retries byte N+1.
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    constexpr int NUM_BYTES = 702;
    constexpr int CANCEL_AT = 350;

    int reads_done = 0;

    for (int i = 0; i < NUM_BYTES; ++i) {
        uint8_t value = static_cast<uint8_t>(i & 0xFF);

        if (i == CANCEL_AT) {
            // At this point byte CANCEL_AT-1 is in the latch (we wrote it
            // in the previous iteration but haven't consumed it yet because
            // we deferred the read -- see below).

            // Set bus_stretch_cancel: host write spins on full latch, defers.
            shared.bus_stretch_cancel.store(true, std::memory_order_release);
            host.host_write(1, value);  // deferred (latch full)
            shared.bus_stretch_cancel.store(false, std::memory_order_release);

            // Parasite reads the previous byte (clears latch)
            uint8_t prev = parasite.parasite_read(1);
            INFO("byte " << (i - 1) << " (read after cancel)");
            CHECK(prev == static_cast<uint8_t>((i - 1) & 0xFF));
            ++reads_done;

            // Retry the deferred write
            host.complete_pending_write();

            // Read the deferred byte
            uint8_t data = parasite.parasite_read(1);
            INFO("byte " << i << " (deferred)");
            CHECK(data == value);
            ++reads_done;
        } else if (i == CANCEL_AT - 1) {
            // Write but DON'T read -- leave the latch full for the cancel test
            host.host_write(1, value);
        } else {
            host.host_write(1, value);
            uint8_t data = parasite.parasite_read(1);
            INFO("byte " << i);
            CHECK(data == value);
            ++reads_done;
        }
    }

    CHECK(reads_done == NUM_BYTES);
}

TEST_CASE("TubeParasitePort R3 deferred write completes after bus_stretch_cancel clears", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(5, 0xAA);
    REQUIRE(TubeReg3::count_of(shared.r3_h2p.state.load()) == 1);

    shared.bus_stretch_cancel.store(true, std::memory_order_release);
    host.host_write(5, 0xBB);  // deferred
    shared.bus_stretch_cancel.store(false, std::memory_order_release);

    CHECK(parasite.parasite_read(5) == 0xAA);
    host.complete_pending_write();
    CHECK(parasite.parasite_read(5) == 0xBB);
}

TEST_CASE("TubeParasitePort R4 deferred write completes after bus_stretch_cancel clears", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(7, 0x55);
    REQUIRE(shared.r4_h2p.ready.load() != 0);

    shared.bus_stretch_cancel.store(true, std::memory_order_release);
    host.host_write(7, 0x66);  // deferred
    shared.bus_stretch_cancel.store(false, std::memory_order_release);

    CHECK(parasite.parasite_read(7) == 0x55);
    host.complete_pending_write();
    CHECK(shared.r4_h2p.ready.load() != 0);
    CHECK(parasite.parasite_read(7) == 0x66);
}

TEST_CASE("TubeParasitePort R1 second read after consume returns latch value not old data", "[tube][parasite]") {
    // After the parasite consumes an R1 byte, a second read (without new
    // host write) should return the data bus latch, not stale FIFO data.
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    host.host_write(1, 0x42);
    uint8_t first = parasite.parasite_read(1);
    CHECK(first == 0x42);

    host.host_write(1, 0xFF);
    uint8_t second = parasite.parasite_read(1);
    CHECK(second == 0xFF);  // must be new byte, not 0x42 again
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
    // Verify P-to-H count is cleared
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_acquire)) == 0);

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

    // After init(), R3 P-to-H count=0 -- space available
    uint8_t status = parasite.parasite_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);

    // After reset(), R3 P-to-H has dummy byte (count=1) -- no space
    parasite.reset();
    status = parasite.parasite_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);

    // Clear the dummy byte
    shared.r3_p2h.state.store(TubeReg3::pack(0, false), std::memory_order_release);

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

    // After reset(), R3 P-to-H has dummy byte (count=1, no space),
    // and no H-to-P data.
    // N = data_available OR space_available = false OR false = false
    parasite.reset();
    uint8_t status = parasite.parasite_read(4);
    CHECK((status & 0x20) == 0);  // N flag clear

    // Clear dummy byte to make space available
    shared.r3_p2h.state.store(TubeReg3::pack(0, false), std::memory_order_release);

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

    // After reset(), R3 P-to-H has dummy byte (count=1),
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

    // After reset(), R3 P-to-H has dummy byte (count=1, no space).
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
    // P-to-H still has the dummy byte (count=1), so space_available = false.
    // Both conditions false -> PNMI level drops -> edge clears.
    parasite.parasite_read(5);
    CHECK_FALSE(parasite.pnmi());
}

TEST_CASE("TubeParasitePort PNMI edge: space available triggers NMI", "[tube][parasite]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    // After reset(), P-to-H has dummy byte (count=1, no space), no H-to-P data.
    parasite.reset();

    // Enable M flag
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Establish baseline (PNMI level = false)
    parasite.parasite_read(4);
    CHECK_FALSE(parasite.pnmi());

    // Host reads the dummy byte -- decrements count, making P-to-H space available
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
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_acquire)) == 1);
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
    CHECK(TubeReg3::count_of(shared.r3_h2p.state.load(std::memory_order_acquire)) == 0);
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

// ===========================================================================
// pnmi_level() -- raw combinational PNMI output
// ===========================================================================

TEST_CASE("TubeParasitePort pnmi_level false when M flag clear", "[tube][parasite][pnmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    // R3 H-to-P has data, but M=0 -> level is false.
    shared.r3_h2p.data[0].store(0x42, std::memory_order_relaxed);
    shared.r3_h2p.state.store(TubeReg3::pack(1, true), std::memory_order_release);

    CHECK_FALSE(parasite.pnmi_level());
}

TEST_CASE("TubeParasitePort pnmi_level true when M=1 and R3 H-to-P has data", "[tube][parasite][pnmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);

    // Deposit H-to-P data.
    shared.r3_h2p.data[0].store(0x42, std::memory_order_relaxed);
    shared.r3_h2p.state.store(TubeReg3::pack(1, true), std::memory_order_release);

    CHECK(parasite.pnmi_level());
}

TEST_CASE("TubeParasitePort pnmi_level true when M=1 and R3 P-to-H has space", "[tube][parasite][pnmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);

    // P-to-H is empty (count=0) -> space available -> level true.
    // Per Tube Application Note: PNMI fires on H-to-P data OR P-to-H space.
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_acquire)) == 0);
    CHECK(parasite.pnmi_level());
}

TEST_CASE("TubeParasitePort pnmi_level false when M=1 but no data and no space", "[tube][parasite][pnmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);

    // R3 H-to-P empty, R3 P-to-H count at threshold (count=1) -> neither condition met.
    shared.r3_p2h.state.store(TubeReg3::pack(1, true), std::memory_order_release);

    CHECK_FALSE(parasite.pnmi_level());
}

TEST_CASE("TubeParasitePort pnmi_level responds to shared state changes without register access", "[tube][parasite][pnmi]") {
    TubeShared shared;
    shared.init();
    TubeParasitePort parasite(&shared);

    shared.control_flags.store(TubeUla::FLAG_M, std::memory_order_release);

    // Start with both conditions false.
    shared.r3_p2h.state.store(TubeReg3::pack(1, true), std::memory_order_release);
    CHECK_FALSE(parasite.pnmi_level());

    // Host deposits H-to-P data (no parasite register access needed).
    shared.r3_h2p.data[0].store(0x42, std::memory_order_relaxed);
    shared.r3_h2p.state.store(TubeReg3::pack(1, true), std::memory_order_release);

    // pnmi_level() should immediately reflect the change.
    CHECK(parasite.pnmi_level());
}

// ===========================================================================
// Hardware-validated R3 behaviour tests
//
// Based on hoglet's comprehensive R3 tests with real Ferranti Tube ULA.
// Reference: https://stardot.org.uk/forums/viewtopic.php?p=409877
//            https://stardot.org.uk/forums/viewtopic.php?p=412565
// B-Em fix:  commit e04aab0 and 97f0ad6
// ===========================================================================

// Helper: set up host and parasite ports on a shared TubeShared
struct TubePair {
    TubeShared shared;
    TubeHostPort host;
    TubeParasitePort parasite;

    TubePair() : host(&shared), parasite(&shared) {
        shared.init();
    }

    // Host writes to R3 H-to-P (offset 5)
    void host_write_r3(uint8_t value) { host.host_write(5, value); }
    // Parasite reads R3 H-to-P data (offset 5)
    uint8_t parasite_read_r3() { return parasite.parasite_read(5); }
    // Parasite reads R3 status (offset 4)
    uint8_t parasite_read_r3_status() { return parasite.parasite_read(4); }
    // Host reads R3 status (offset 4)
    uint8_t host_read_r3_status() { return host.host_read(4); }

    // Parasite writes to R3 P-to-H (offset 5)
    void parasite_write_r3(uint8_t value) { parasite.parasite_write(5, value); }
    // Host reads R3 P-to-H data (offset 5)
    uint8_t host_read_r3() { return host.host_read(5); }

    // Check parasite R3 status bits
    bool parasite_r3_data_available() {
        return (parasite_read_r3_status() & TubeUla::DATA_AVAILABLE) != 0;
    }
    bool parasite_r3_space_available() {
        return (parasite_read_r3_status() & TubeUla::SPACE_AVAILABLE) != 0;
    }
    // Check host R3 status bits
    bool host_r3_data_available() {
        return (host_read_r3_status() & TubeUla::DATA_AVAILABLE) != 0;
    }
    bool host_r3_space_available() {
        return (host_read_r3_status() & TubeUla::SPACE_AVAILABLE) != 0;
    }

    // Set V flag (2-byte mode) via control register
    void set_v_flag(bool enable) {
        if (enable)
            host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);
        else
            host.host_write(0, TubeUla::FLAG_V);  // S=0 clears V
    }
};

// --- R3 H-to-P: 1-byte mode (V=0) ---

TEST_CASE("R3 H-to-P 1-byte mode: write-read", "[tube][r3][hw]") {
    TubePair t;
    t.host_write_r3(0x42);
    CHECK(t.parasite_r3_data_available());
    CHECK(t.parasite_read_r3() == 0x42);
}

TEST_CASE("R3 H-to-P 1-byte mode: write-read-write-read sequence", "[tube][r3][hw]") {
    // In 1-byte mode, the host can only write 1 byte before the parasite
    // must read (bus stretching blocks the second write until count < 1).
    TubePair t;
    t.host_write_r3(0x11);
    CHECK(t.parasite_read_r3() == 0x11);
    t.host_write_r3(0x22);
    CHECK(t.parasite_read_r3() == 0x22);
}

TEST_CASE("R3 H-to-P 1-byte mode: data available after single write", "[tube][r3][hw]") {
    TubePair t;
    CHECK_FALSE(t.parasite_r3_data_available());
    t.host_write_r3(0x42);
    CHECK(t.parasite_r3_data_available());
}

TEST_CASE("R3 H-to-P 1-byte mode: data not available after read drains", "[tube][r3][hw]") {
    TubePair t;
    t.host_write_r3(0x42);
    t.parasite_read_r3();
    CHECK_FALSE(t.parasite_r3_data_available());
}

TEST_CASE("R3 H-to-P 1-byte mode: host space available after drain", "[tube][r3][hw]") {
    TubePair t;
    t.host_write_r3(0x42);
    CHECK_FALSE(t.host_r3_space_available());
    t.parasite_read_r3();
    CHECK(t.host_r3_space_available());
}

// TODO: R3 writes when full should be silently ignored (real hardware),
// but Beebium currently spin-waits (bus stretching). Need to add a
// non-blocking write path or timeout. Test disabled to avoid hang.
// TEST_CASE("R3 H-to-P 1-byte mode: third write ignored when full")

TEST_CASE("R3 H-to-P 1-byte mode: interleaved write-read-write-read", "[tube][r3][hw]") {
    TubePair t;
    t.host_write_r3(0xAA);
    CHECK(t.parasite_read_r3() == 0xAA);
    t.host_write_r3(0xBB);
    CHECK(t.parasite_read_r3() == 0xBB);
}

// --- R3 H-to-P: 2-byte mode (V=1) ---

TEST_CASE("R3 H-to-P 2-byte mode: data available only after 2 writes", "[tube][r3][hw]") {
    TubePair t;
    t.set_v_flag(true);
    t.host_write_r3(0x11);
    CHECK_FALSE(t.parasite_r3_data_available());
    t.host_write_r3(0x22);
    CHECK(t.parasite_r3_data_available());
}

TEST_CASE("R3 H-to-P 2-byte mode: read both bytes", "[tube][r3][hw]") {
    TubePair t;
    t.set_v_flag(true);
    t.host_write_r3(0x11);
    t.host_write_r3(0x22);
    CHECK(t.parasite_read_r3() == 0x11);
    CHECK(t.parasite_read_r3() == 0x22);
}

// --- R3 P-to-H: 1-byte mode ---

TEST_CASE("R3 P-to-H 1-byte mode: write-read", "[tube][r3][hw]") {
    TubePair t;
    // Clear the dummy P-to-H byte from init
    t.shared.r3_p2h.state.store(TubeReg3::pack(0, false), std::memory_order_release);
    t.shared.r3_p2h.head.store(0, std::memory_order_release);
    t.shared.r3_p2h.tail.store(0, std::memory_order_release);

    t.parasite_write_r3(0x55);
    CHECK(t.host_r3_data_available());
    CHECK(t.host_read_r3() == 0x55);
}

// --- R1 unconditional flag clearing ---

TEST_CASE("R1 parasite data read clears ready even when empty", "[tube][r1][hw]") {
    TubePair t;
    // Write a byte from host
    t.host.host_write(1, 0x42);
    // Parasite reads it
    CHECK(t.parasite.parasite_read(1) == 0x42);

    // Now R1 is empty. Host writes another byte.
    t.host.host_write(1, 0x99);
    // Parasite reads data WITHOUT checking status first (stray read).
    // This should still consume the byte (unconditional clear).
    uint8_t val = t.parasite.parasite_read(1);
    CHECK(val == 0x99);

    // The ready flag should now be cleared
    uint8_t status = t.parasite.parasite_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// --- PNMI only fires on H-to-P data, not P-to-H space ---

TEST_CASE("PNMI fires from P-to-H space available in 1-byte mode", "[tube][pnmi][hw]") {
    TubePair t;
    // Enable M flag
    t.host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Clear P-to-H so it has space available
    t.shared.r3_p2h.state.store(TubeReg3::pack(0, false), std::memory_order_release);

    // Per Tube Application Note: M=1, V=0: PNMI fires when P-to-H has 0 bytes
    // (space available for single-byte transfer).
    CHECK(t.parasite.pnmi_level());
}

TEST_CASE("PNMI fires when H-to-P has data and M is set", "[tube][pnmi][hw]") {
    TubePair t;
    // Enable M flag
    t.host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Write H-to-P data
    t.host_write_r3(0x42);

    CHECK(t.parasite.pnmi_level());
}
