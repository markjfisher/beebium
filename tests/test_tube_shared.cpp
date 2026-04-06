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

#include <beebium/tube/TubeShared.hpp>
#include <beebium/tube/TubeConcepts.hpp>
#include <beebium/tube/TubeUla.hpp>

using namespace beebium;

// ===========================================================================
// TubeHostInterface concept
// ===========================================================================

TEST_CASE("TubeHostInterface: TubeUla satisfies the concept", "[tube][concepts]") {
    CHECK(TubeHostInterface<TubeUla>);
}

// ===========================================================================
// TubeShared layout and initialisation
// ===========================================================================

TEST_CASE("TubeShared: standard layout", "[tube][shared]") {
    CHECK(std::is_standard_layout_v<TubeShared>);
}

TEST_CASE("TubeShared: default construction sets valid header", "[tube][shared]") {
    TubeShared shared;
    CHECK(shared.magic == TUBE_SHARED_MAGIC);
    CHECK(shared.version == TUBE_SHARED_VERSION);
    CHECK(shared.valid());
}

TEST_CASE("TubeShared: init clears all fields", "[tube][shared]") {
    TubeShared shared;

    // Dirty some fields
    shared.control_flags.store(0xFF, std::memory_order_relaxed);
    shared.r1_h2p.value.store(0xAA, std::memory_order_relaxed);
    shared.r1_h2p.ready.store(1, std::memory_order_relaxed);
    shared.r4_p2h.value.store(0xBB, std::memory_order_relaxed);
    shared.r4_p2h.ready.store(1, std::memory_order_relaxed);
    shared.r1_p2h.count.store(5, std::memory_order_relaxed);
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown), std::memory_order_relaxed);

    shared.init();

    CHECK(shared.valid());
    CHECK(shared.control_flags.load(std::memory_order_relaxed) == 0);

    // H-to-P latches cleared
    CHECK(shared.r1_h2p.value.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r1_h2p.ready.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r2_h2p.ready.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r4_h2p.ready.load(std::memory_order_relaxed) == 0);

    // R3 cleared
    CHECK(TubeReg3::count_of(shared.r3_h2p.state.load(std::memory_order_relaxed)) == 0);
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_relaxed)) == 0);

    // P-to-H cleared
    CHECK(shared.r1_p2h.count.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r1_p2h.head.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r1_p2h.tail.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r4_p2h.value.load(std::memory_order_relaxed) == 0);
    CHECK(shared.r4_p2h.ready.load(std::memory_order_relaxed) == 0);

    // Lifecycle mailbox cleared
    CHECK(shared.host_command.load(std::memory_order_relaxed) == 0);
    CHECK(shared.parasite_ack.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("TubeShared: valid rejects wrong magic", "[tube][shared]") {
    TubeShared shared;
    shared.magic = 0xDEADBEEF;
    CHECK_FALSE(shared.valid());
}

TEST_CASE("TubeShared: valid rejects wrong version", "[tube][shared]") {
    TubeShared shared;
    shared.version = 99;
    CHECK_FALSE(shared.valid());
}

// ===========================================================================
// TubeShared sub-structure sizes
// ===========================================================================

TEST_CASE("TubeShared: TubeLatch is lock-free", "[tube][shared]") {
    TubeLatch latch;
    CHECK(latch.value.is_lock_free());
    CHECK(latch.ready.is_lock_free());
}

TEST_CASE("TubeShared: TubeFifo24 atomics are lock-free", "[tube][shared]") {
    TubeFifo24 fifo;
    CHECK(fifo.head.is_lock_free());
    CHECK(fifo.tail.is_lock_free());
    CHECK(fifo.count.is_lock_free());
    CHECK(fifo.data[0].is_lock_free());
}

TEST_CASE("TubeShared: control_flags is lock-free", "[tube][shared]") {
    TubeShared shared;
    CHECK(shared.control_flags.is_lock_free());
}

// ===========================================================================
// Lifecycle mailbox
// ===========================================================================

TEST_CASE("TubeShared: lifecycle command round-trip", "[tube][shared]") {
    TubeShared shared;
    shared.init();

    // Host sends reset command
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Reset), std::memory_order_release);

    // Parasite reads it
    auto cmd = static_cast<TubeLifecycleCommand>(
        shared.host_command.load(std::memory_order_acquire));
    CHECK(cmd == TubeLifecycleCommand::Reset);

    // Parasite acknowledges
    shared.parasite_ack.store(
        static_cast<uint8_t>(TubeLifecycleAck::ResetDone), std::memory_order_release);

    // Host reads acknowledgement
    auto ack = static_cast<TubeLifecycleAck>(
        shared.parasite_ack.load(std::memory_order_acquire));
    CHECK(ack == TubeLifecycleAck::ResetDone);
}

TEST_CASE("TubeShared: lifecycle shutdown sequence", "[tube][shared]") {
    TubeShared shared;
    shared.init();

    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown), std::memory_order_release);

    auto cmd = static_cast<TubeLifecycleCommand>(
        shared.host_command.load(std::memory_order_acquire));
    CHECK(cmd == TubeLifecycleCommand::Shutdown);

    shared.parasite_ack.store(
        static_cast<uint8_t>(TubeLifecycleAck::Exiting), std::memory_order_release);

    auto ack = static_cast<TubeLifecycleAck>(
        shared.parasite_ack.load(std::memory_order_acquire));
    CHECK(ack == TubeLifecycleAck::Exiting);
}

// ===========================================================================
// TubeLatch basic operations
// ===========================================================================

TEST_CASE("TubeShared: latch write-then-read pattern", "[tube][shared]") {
    TubeLatch latch;

    // Writer deposits value and signals ready
    latch.value.store(0x42, std::memory_order_relaxed);
    latch.ready.store(1, std::memory_order_release);

    // Reader sees data available
    CHECK(latch.ready.load(std::memory_order_acquire) == 1);
    uint8_t val = latch.value.load(std::memory_order_relaxed);
    CHECK(val == 0x42);

    // Reader clears ready
    latch.ready.store(0, std::memory_order_release);
    CHECK(latch.ready.load(std::memory_order_acquire) == 0);
}

// ===========================================================================
// TubeFifo24 basic operations
// ===========================================================================

TEST_CASE("TubeShared: fifo24 enqueue and dequeue", "[tube][shared]") {
    TubeFifo24 fifo;

    // Enqueue 3 bytes (parasite writes at tail)
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t tail = fifo.tail.load(std::memory_order_relaxed);
        fifo.data[tail].store(0xA0 + i, std::memory_order_relaxed);
        fifo.tail.store((tail + 1) % 24, std::memory_order_relaxed);
        fifo.count.fetch_add(1, std::memory_order_release);
    }

    CHECK(fifo.count.load(std::memory_order_acquire) == 3);

    // Dequeue 3 bytes (host reads from head)
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t head = fifo.head.load(std::memory_order_relaxed);
        uint8_t val = fifo.data[head].load(std::memory_order_relaxed);
        CHECK(val == 0xA0 + i);
        fifo.head.store((head + 1) % 24, std::memory_order_relaxed);
        fifo.count.fetch_sub(1, std::memory_order_release);
    }

    CHECK(fifo.count.load(std::memory_order_acquire) == 0);
}

TEST_CASE("TubeShared: fifo24 wraps around", "[tube][shared]") {
    TubeFifo24 fifo;

    // Fill and drain 23 bytes to advance head/tail near the end
    for (uint8_t i = 0; i < 23; ++i) {
        uint8_t tail = fifo.tail.load(std::memory_order_relaxed);
        fifo.data[tail].store(i, std::memory_order_relaxed);
        fifo.tail.store((tail + 1) % 24, std::memory_order_release);
        fifo.count.fetch_add(1, std::memory_order_release);
    }
    for (uint8_t i = 0; i < 23; ++i) {
        uint8_t head = fifo.head.load(std::memory_order_relaxed);
        fifo.head.store((head + 1) % 24, std::memory_order_release);
        fifo.count.fetch_sub(1, std::memory_order_release);
    }

    // Now head=23, tail=23. Enqueue 3 more -- should wrap around index 0.
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t tail = fifo.tail.load(std::memory_order_relaxed);
        fifo.data[tail].store(0xF0 + i, std::memory_order_relaxed);
        fifo.tail.store((tail + 1) % 24, std::memory_order_release);
        fifo.count.fetch_add(1, std::memory_order_release);
    }

    CHECK(fifo.count.load(std::memory_order_acquire) == 3);
    CHECK(fifo.head.load(std::memory_order_relaxed) == 23);
    CHECK(fifo.tail.load(std::memory_order_relaxed) == 2);  // wrapped: (23+3) % 24 = 2

    // Dequeue and verify
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t head = fifo.head.load(std::memory_order_relaxed);
        uint8_t val = fifo.data[head].load(std::memory_order_relaxed);
        CHECK(val == 0xF0 + i);
        fifo.head.store((head + 1) % 24, std::memory_order_release);
        fifo.count.fetch_sub(1, std::memory_order_release);
    }

    CHECK(fifo.count.load(std::memory_order_acquire) == 0);
}
