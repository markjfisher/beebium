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

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeConcepts.hpp>
#include <beebium/tube/TubeUla.hpp>

using namespace beebium;

// ===========================================================================
// Concept satisfaction
// ===========================================================================

TEST_CASE("TubeHostPort: satisfies TubeHostInterface concept", "[tube][host_port]") {
    CHECK(TubeHostInterface<TubeHostPort>);
}

// ===========================================================================
// Construction and reset
// ===========================================================================

TEST_CASE("TubeHostPort: constructed from TubeShared pointer", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    CHECK(port.shared() == &shared);
}

TEST_CASE("TubeHostPort: reset clears control flags", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Set some control flags
    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);
    REQUIRE(port.control_flags() != 0);

    port.reset();
    CHECK(port.control_flags() == 0);
}

TEST_CASE("TubeHostPort: reset clears H-to-P register data", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Write data to H-to-P registers
    port.host_write(1, 0xAA);  // R1
    port.host_write(3, 0xBB);  // R2
    port.host_write(7, 0xCC);  // R4

    port.reset();

    // H-to-P latches should be empty (space available)
    uint8_t r1_stat = port.host_read(0);
    CHECK((r1_stat & TubeUla::SPACE_AVAILABLE) != 0);
    CHECK((r1_stat & TubeUla::DATA_AVAILABLE) == 0);
}

TEST_CASE("TubeHostPort: reset sends lifecycle command", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.reset();

    auto cmd = static_cast<TubeLifecycleCommand>(
        shared.host_command.load(std::memory_order_acquire));
    CHECK(cmd == TubeLifecycleCommand::Reset);
}

TEST_CASE("TubeHostPort: reset initialises R3 P-to-H with dummy byte", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.reset();

    // R3 P-to-H should have count=1 (dummy byte to prevent spurious PNMI,
    // matching TubeUla::soft_reset behaviour).
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_relaxed)) == 1);
}

// ===========================================================================
// Control flag register (offset 0)
// ===========================================================================

TEST_CASE("TubeHostPort: set control flags with S=1", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M);

    CHECK((port.control_flags() & TubeUla::FLAG_Q) != 0);
    CHECK((port.control_flags() & TubeUla::FLAG_M) != 0);
}

TEST_CASE("TubeHostPort: clear control flags with S=0", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Set Q and M
    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M);
    REQUIRE((port.control_flags() & TubeUla::FLAG_Q) != 0);

    // Clear Q only (S=0)
    port.host_write(0, TubeUla::FLAG_Q);
    CHECK((port.control_flags() & TubeUla::FLAG_Q) == 0);
    CHECK((port.control_flags() & TubeUla::FLAG_M) != 0);  // M unchanged
}

TEST_CASE("TubeHostPort: T flag triggers soft reset without being stored", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Write data to R1 H-to-P
    port.host_write(1, 0x42);
    REQUIRE(shared.r1_h2p.ready.load(std::memory_order_relaxed) == 1);

    // Set T flag -- should clear registers but T is not stored in flags
    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);

    CHECK(shared.r1_h2p.ready.load(std::memory_order_relaxed) == 0);
    // T (bit 6) is NOT stored in control flags (it's an action)
    CHECK((port.control_flags() & TubeUla::FLAG_T) == 0);
}

TEST_CASE("TubeHostPort: control flags visible in R1 status read", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);

    uint8_t status = port.host_read(0);
    CHECK((status & TubeUla::FLAG_Q) != 0);
    CHECK((status & TubeUla::FLAG_I) != 0);
}

// ===========================================================================
// R1: H-to-P latch, P-to-H 24-byte FIFO
// ===========================================================================

TEST_CASE("TubeHostPort: R1 status after reset shows space available, no data", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    uint8_t status = port.host_read(0);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

TEST_CASE("TubeHostPort: R1 H-to-P write sets latch", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(1, 0xAB);

    CHECK(shared.r1_h2p.value.load(std::memory_order_acquire) == 0xAB);
    CHECK(shared.r1_h2p.ready.load(std::memory_order_acquire) == 1);

    // Status should show H-to-P is full (no space)
    uint8_t status = port.host_read(0);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

TEST_CASE("TubeHostPort: R1 P-to-H FIFO read (parasite enqueues, host dequeues)", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Simulate parasite writing 3 bytes to P-to-H FIFO
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t tail = shared.r1_p2h.tail.load(std::memory_order_relaxed);
        shared.r1_p2h.data[tail].store(0x10 + i, std::memory_order_relaxed);
        shared.r1_p2h.tail.store((tail + 1) % 24, std::memory_order_relaxed);
        shared.r1_p2h.count.fetch_add(1, std::memory_order_release);
    }

    // Status should show data available
    uint8_t status = port.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Host reads 3 bytes via register
    CHECK(port.host_read(1) == 0x10);
    CHECK(port.host_read(1) == 0x11);
    CHECK(port.host_read(1) == 0x12);

    // FIFO empty, status reflects this
    status = port.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

TEST_CASE("TubeHostPort: R1 P-to-H empty read returns 0", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    CHECK(port.host_read(1) == 0);
}

// ===========================================================================
// R2: 1-byte latch in each direction
// ===========================================================================

TEST_CASE("TubeHostPort: R2 H-to-P write and status", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // R2 status: space available initially
    uint8_t status = port.host_read(2);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);

    // Write to H-to-P
    port.host_write(3, 0xCD);

    // Verify latch state
    CHECK(shared.r2_h2p.value.load(std::memory_order_acquire) == 0xCD);
    CHECK(shared.r2_h2p.ready.load(std::memory_order_acquire) == 1);

    // Status: no space (full)
    status = port.host_read(2);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

TEST_CASE("TubeHostPort: R2 P-to-H read and consume", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Simulate parasite writing to P-to-H latch
    shared.r2_p2h.value.store(0xEF, std::memory_order_relaxed);
    shared.r2_p2h.ready.store(1, std::memory_order_release);

    // Status: data available
    uint8_t status = port.host_read(2);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read and consume
    CHECK(port.host_read(3) == 0xEF);

    // Ready should be cleared
    CHECK(shared.r2_p2h.ready.load(std::memory_order_acquire) == 0);

    // Status: no data
    status = port.host_read(2);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// R3: 2-byte shift register
// ===========================================================================

TEST_CASE("TubeHostPort: R3 H-to-P write in 1-byte mode", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // V=0 -> 1-byte mode (threshold=1)
    // Write one byte -- count reaches threshold
    port.host_write(5, 0x33);

    CHECK(shared.r3_h2p.data[0].load(std::memory_order_relaxed) == 0x33);
    CHECK(TubeReg3::count_of(shared.r3_h2p.state.load(std::memory_order_relaxed)) == 1);
}

TEST_CASE("TubeHostPort: R3 H-to-P write in 2-byte mode", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Set V=1 for 2-byte mode
    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);

    // Write first byte -- count below threshold
    port.host_write(5, 0x44);
    CHECK(TubeReg3::count_of(shared.r3_h2p.state.load(std::memory_order_relaxed)) == 1);

    // Write second byte -- count reaches threshold
    port.host_write(5, 0x55);
    CHECK(TubeReg3::count_of(shared.r3_h2p.state.load(std::memory_order_relaxed)) == 2);
}

TEST_CASE("TubeHostPort: R3 P-to-H read and consume", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Simulate parasite writing 2 bytes to P-to-H
    shared.r3_p2h.data[0].store(0x66, std::memory_order_relaxed);
    shared.r3_p2h.data[1].store(0x77, std::memory_order_relaxed);
    shared.r3_p2h.head.store(0, std::memory_order_relaxed);
    shared.r3_p2h.tail.store(0, std::memory_order_relaxed);  // wrapped after writing 2
    shared.r3_p2h.state.store(TubeReg3::pack(2, true), std::memory_order_release);

    // Status: data available
    uint8_t status = port.host_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read first byte (shifts second down)
    CHECK(port.host_read(5) == 0x66);
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_relaxed)) == 1);

    // Read second byte
    CHECK(port.host_read(5) == 0x77);
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_relaxed)) == 0);
}

TEST_CASE("TubeHostPort: R3 status reflects H-to-P space", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Initially: space available (no pending H-to-P transfer)
    uint8_t status = port.host_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);

    // Write in 1-byte mode (V=0, threshold=1) -> count reaches threshold
    port.host_write(5, 0x88);

    // No space available (transfer awaiting parasite read)
    status = port.host_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

// ===========================================================================
// R4: 1-byte latch in each direction
// ===========================================================================

TEST_CASE("TubeHostPort: R4 H-to-P write and P-to-H read", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Write to H-to-P
    port.host_write(7, 0x99);
    CHECK(shared.r4_h2p.value.load(std::memory_order_acquire) == 0x99);
    CHECK(shared.r4_h2p.ready.load(std::memory_order_acquire) == 1);

    // Simulate parasite writing to P-to-H
    shared.r4_p2h.value.store(0xAA, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_release);

    // Host reads P-to-H
    CHECK(port.host_read(7) == 0xAA);
    CHECK(shared.r4_p2h.ready.load(std::memory_order_acquire) == 0);
}

// ===========================================================================
// HIRQ computation
// ===========================================================================

TEST_CASE("TubeHostPort: hirq false when Q=0", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Parasite writes R4 P-to-H data but Q is not set
    shared.r4_p2h.value.store(0x42, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_release);

    CHECK_FALSE(port.hirq());
}

TEST_CASE("TubeHostPort: hirq false when Q=1 but no R4 data", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    CHECK_FALSE(port.hirq());
}

TEST_CASE("TubeHostPort: hirq true when Q=1 and R4 P-to-H has data", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    shared.r4_p2h.value.store(0x42, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_release);

    CHECK(port.hirq());
}

TEST_CASE("TubeHostPort: hirq deasserted when R4 data consumed", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    shared.r4_p2h.value.store(0x42, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_release);
    REQUIRE(port.hirq());

    port.host_read(7);  // consume R4 data
    CHECK_FALSE(port.hirq());
}

TEST_CASE("TubeHostPort: hirq deasserted when Q cleared", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    shared.r4_p2h.value.store(0x42, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_release);
    REQUIRE(port.hirq());

    port.host_write(0, TubeUla::FLAG_Q);  // S=0, clear Q
    CHECK_FALSE(port.hirq());
}

// ===========================================================================
// Cross-validation: TubeHostPort vs TubeUla
// ===========================================================================
//
// These tests perform the same operations on both TubeUla and TubeHostPort
// and verify they produce identical results from the host's perspective.

TEST_CASE("TubeHostPort: R1 status matches TubeUla after reset", "[tube][host_port][cross]") {
    TubeUla ula;
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    CHECK(port.host_read(0) == ula.host_read(0));
}

TEST_CASE("TubeHostPort: R1 H-to-P write then status matches TubeUla", "[tube][host_port][cross]") {
    TubeUla ula;
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    ula.host_write(1, 0x42);
    port.host_write(1, 0x42);

    CHECK(port.host_read(0) == ula.host_read(0));
}

TEST_CASE("TubeHostPort: control flag set/clear matches TubeUla", "[tube][host_port][cross]") {
    TubeUla ula;
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Set Q, I, M
    ula.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I | TubeUla::FLAG_M);
    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I | TubeUla::FLAG_M);

    CHECK(port.host_read(0) == ula.host_read(0));

    // Clear I
    ula.host_write(0, TubeUla::FLAG_I);
    port.host_write(0, TubeUla::FLAG_I);

    CHECK(port.host_read(0) == ula.host_read(0));
}

TEST_CASE("TubeHostPort: R2/R4 status matches TubeUla", "[tube][host_port][cross]") {
    TubeUla ula;
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // R2 status after reset
    CHECK(port.host_read(2) == ula.host_read(2));
    // R4 status after reset
    CHECK(port.host_read(6) == ula.host_read(6));

    // Write to H-to-P latches
    ula.host_write(3, 0xBB);
    port.host_write(3, 0xBB);
    CHECK(port.host_read(2) == ula.host_read(2));

    ula.host_write(7, 0xCC);
    port.host_write(7, 0xCC);
    CHECK(port.host_read(6) == ula.host_read(6));
}

TEST_CASE("TubeHostPort: HIRQ matches TubeUla", "[tube][host_port][cross]") {
    TubeUla ula;
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    // Set Q
    ula.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    port.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);

    // No data yet
    CHECK(port.hirq() == ula.hirq());

    // Parasite writes R4 P-to-H
    ula.parasite_write(7, 0x42);
    shared.r4_p2h.value.store(0x42, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_release);

    CHECK(port.hirq() == ula.hirq());
    CHECK(port.hirq() == true);
}

// ===========================================================================
// Address mirroring
// ===========================================================================

TEST_CASE("TubeHostPort: offset masked to 3 bits", "[tube][host_port]") {
    TubeShared shared;
    shared.init();
    TubeHostPort port(&shared);

    port.host_write(1, 0x42);

    // Offset 8 should read the same as offset 0 (3-bit mask)
    CHECK(port.host_read(0) == port.host_read(8));
}
