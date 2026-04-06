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

// Cross-thread tests for TubeShared and TubeHostPort.
//
// These are Tier 2 tests from the design doc: two threads share a single
// TubeShared instance (one acting as host, one as parasite) and verify
// that atomic ordering makes data visible correctly across threads.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeShared.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace beebium;

// ---------------------------------------------------------------------------
// Parasite-side helpers operating directly on TubeShared atomics.
// These mirror what a real parasite process would do.
// ---------------------------------------------------------------------------

namespace {

// Parasite reads H-to-P latch: returns value and clears ready.
uint8_t parasite_read_latch(TubeLatch& latch) {
    uint8_t val = latch.value.load(std::memory_order_acquire);
    latch.ready.store(0, std::memory_order_release);
    return val;
}

// Parasite writes P-to-H latch: stores value and sets ready.
void parasite_write_latch(TubeLatch& latch, uint8_t value) {
    latch.value.store(value, std::memory_order_relaxed);
    latch.ready.store(1, std::memory_order_release);
}

// Parasite enqueues a byte into the R1 P-to-H FIFO.
// Returns true if enqueued, false if full.
bool parasite_enqueue_r1(TubeFifo24& fifo, uint8_t value) {
    uint8_t count = fifo.count.load(std::memory_order_acquire);
    if (count >= 24)
        return false;

    uint8_t tail = fifo.tail.load(std::memory_order_relaxed);
    fifo.data[tail].store(value, std::memory_order_relaxed);
    fifo.tail.store((tail + 1) % 24, std::memory_order_relaxed);
    fifo.count.fetch_add(1, std::memory_order_release);
    return true;
}

// Parasite writes to R3 P-to-H register.
// Returns true if written, false if full.
bool parasite_write_r3(TubeReg3& reg, uint8_t value) {
    uint16_t old_state = reg.state.load(std::memory_order_acquire);
    uint8_t count = TubeReg3::count_of(old_state);
    if (count >= 2)
        return false;

    uint8_t tail = reg.tail.load(std::memory_order_relaxed);
    reg.data[tail].store(value, std::memory_order_relaxed);
    reg.tail.store(tail ^ 1, std::memory_order_relaxed);

    // CAS to atomically increment count and set pending.
    uint16_t new_state;
    do {
        uint8_t new_count = TubeReg3::count_of(old_state) + 1;
        new_state = TubeReg3::pack(new_count, true);
    } while (!reg.state.compare_exchange_weak(
        old_state, new_state, std::memory_order_release, std::memory_order_acquire));
    return true;
}

}  // namespace

// ===========================================================================
// Cross-thread visibility: H-to-P latch
// ===========================================================================

TEST_CASE("TubeShared cross-thread: H-to-P latch visible to parasite", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    std::atomic<bool> host_written{false};
    std::atomic<bool> parasite_done{false};
    uint8_t parasite_saw = 0;

    std::thread parasite([&] {
        // Wait for host to write
        while (!host_written.load(std::memory_order_acquire)) {}

        // Read the latch
        if (shared.r1_h2p.ready.load(std::memory_order_acquire) != 0) {
            parasite_saw = parasite_read_latch(shared.r1_h2p);
        }
        parasite_done.store(true, std::memory_order_release);
    });

    // Host writes R1 H-to-P
    host.host_write(1, 0xAB);
    host_written.store(true, std::memory_order_release);

    // Wait for parasite
    while (!parasite_done.load(std::memory_order_acquire)) {}
    parasite.join();

    CHECK(parasite_saw == 0xAB);
    // Parasite consumed the latch -- ready should be cleared
    CHECK(shared.r1_h2p.ready.load(std::memory_order_acquire) == 0);
}

// ===========================================================================
// Cross-thread visibility: P-to-H latch
// ===========================================================================

TEST_CASE("TubeShared cross-thread: P-to-H latch visible to host", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    std::atomic<bool> parasite_written{false};
    uint8_t host_saw = 0;

    std::thread parasite([&] {
        parasite_write_latch(shared.r4_p2h, 0xCD);
        parasite_written.store(true, std::memory_order_release);
    });

    // Wait for parasite to write
    while (!parasite_written.load(std::memory_order_acquire)) {}

    // Host reads R4 status -- should show data available
    uint8_t status = host.host_read(6);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Host reads R4 data
    host_saw = host.host_read(7);
    parasite.join();

    CHECK(host_saw == 0xCD);
    CHECK(shared.r4_p2h.ready.load(std::memory_order_acquire) == 0);
}

// ===========================================================================
// Cross-thread visibility: control flags
// ===========================================================================

TEST_CASE("TubeShared cross-thread: control flags visible to parasite", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    std::atomic<bool> host_written{false};
    std::atomic<bool> parasite_done{false};
    uint8_t parasite_saw_flags = 0;

    std::thread parasite([&] {
        while (!host_written.load(std::memory_order_acquire)) {}
        parasite_saw_flags = shared.control_flags.load(std::memory_order_acquire);
        parasite_done.store(true, std::memory_order_release);
    });

    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M);
    host_written.store(true, std::memory_order_release);

    while (!parasite_done.load(std::memory_order_acquire)) {}
    parasite.join();

    CHECK((parasite_saw_flags & TubeUla::FLAG_Q) != 0);
    CHECK((parasite_saw_flags & TubeUla::FLAG_M) != 0);
}

// ===========================================================================
// Cross-thread HIRQ: parasite writes R4, host sees IRQ
// ===========================================================================

TEST_CASE("TubeShared cross-thread: HIRQ asserted when parasite writes R4", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    // Set Q flag first (from host thread)
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    REQUIRE_FALSE(host.hirq());

    std::atomic<bool> parasite_written{false};

    std::thread parasite([&] {
        parasite_write_latch(shared.r4_p2h, 0x42);
        parasite_written.store(true, std::memory_order_release);
    });

    while (!parasite_written.load(std::memory_order_acquire)) {}
    parasite.join();

    CHECK(host.hirq());

    // Consume the data -- HIRQ should deassert
    host.host_read(7);
    CHECK_FALSE(host.hirq());
}

// ===========================================================================
// Cross-thread R1 FIFO: parasite enqueues, host dequeues
// ===========================================================================

TEST_CASE("TubeShared cross-thread: R1 FIFO single byte", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    std::atomic<bool> parasite_written{false};

    std::thread parasite([&] {
        parasite_enqueue_r1(shared.r1_p2h, 0x77);
        parasite_written.store(true, std::memory_order_release);
    });

    while (!parasite_written.load(std::memory_order_acquire)) {}
    parasite.join();

    uint8_t status = host.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);
    CHECK(host.host_read(1) == 0x77);
}

TEST_CASE("TubeShared cross-thread: R1 FIFO stress test", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    constexpr int NUM_BYTES = 1000;
    std::vector<uint8_t> received;
    received.reserve(NUM_BYTES);

    std::atomic<bool> parasite_done{false};

    std::thread parasite([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t byte = static_cast<uint8_t>(i & 0xFF);
            // Spin until there's space in the FIFO
            while (!parasite_enqueue_r1(shared.r1_p2h, byte)) {
                // FIFO full, wait for host to drain
            }
        }
        parasite_done.store(true, std::memory_order_release);
    });

    // Host dequeues bytes as they arrive
    int count = 0;
    while (count < NUM_BYTES) {
        uint8_t fifo_count = shared.r1_p2h.count.load(std::memory_order_acquire);
        if (fifo_count > 0) {
            uint8_t byte = host.host_read(1);
            received.push_back(byte);
            count++;
        }
    }

    parasite.join();

    // Verify all bytes arrived in order
    REQUIRE(received.size() == NUM_BYTES);
    for (int i = 0; i < NUM_BYTES; ++i) {
        CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
    }

    // FIFO should be empty
    CHECK(shared.r1_p2h.count.load(std::memory_order_acquire) == 0);
}

// ===========================================================================
// Cross-thread R1 H-to-P latch: sustained transfer (CE2023 pattern)
// ===========================================================================

TEST_CASE("TubeShared cross-thread: R1 H-to-P sustained transfer via ports", "[tube][shared][threads]") {
    // Simulates the CE2023 R1 polled transfer: host writes 702 bytes
    // through TubeHostPort (bus-stretch spin on full latch), parasite
    // reads through TubeParasitePort (poll status, then read data).
    // Both run as fast as possible. Every byte must arrive correctly.

    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    constexpr int NUM_BYTES = 702;
    std::vector<uint8_t> received;
    received.reserve(NUM_BYTES);

    std::thread parasite_thread([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            // Poll R1 status until data available (bit 7)
            while ((parasite.parasite_read(0) & TubeUla::DATA_AVAILABLE) == 0) {
                // spin -- mimics BIT $FEF8; BPL loop
            }
            // Read R1 data
            uint8_t byte = parasite.parasite_read(1);
            received.push_back(byte);
        }
    });

    // Host writes bytes through bus-stretched R1
    for (int i = 0; i < NUM_BYTES; ++i) {
        host.host_write(1, static_cast<uint8_t>(i & 0xFF));
    }

    parasite_thread.join();

    // Verify all bytes arrived in order
    REQUIRE(received.size() == NUM_BYTES);
    int first_error = -1;
    for (int i = 0; i < NUM_BYTES; ++i) {
        if (received[i] != static_cast<uint8_t>(i & 0xFF)) {
            if (first_error < 0) first_error = i;
            INFO("byte " << i << ": expected $" << std::hex << (i & 0xFF)
                 << " got $" << std::hex << static_cast<int>(received[i]));
            CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
        }
    }
    if (first_error >= 0) {
        FAIL("First corrupted byte at index " << first_error);
    }
}

TEST_CASE("TubeShared cross-thread: R1 H-to-P repeated transfer stress", "[tube][shared][threads]") {
    // Run the R1 H-to-P transfer 100 times to catch non-deterministic races.

    TubeShared shared;
    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    constexpr int NUM_BYTES = 702;
    constexpr int ITERATIONS = 100;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        shared.init();
        std::vector<uint8_t> received;
        received.reserve(NUM_BYTES);

        // Offset the byte values each iteration to detect stale data
        uint8_t base = static_cast<uint8_t>(iter * 7);

        std::thread parasite_thread([&] {
            for (int i = 0; i < NUM_BYTES; ++i) {
                while ((parasite.parasite_read(0) & TubeUla::DATA_AVAILABLE) == 0) {}
                received.push_back(parasite.parasite_read(1));
            }
        });

        for (int i = 0; i < NUM_BYTES; ++i) {
            host.host_write(1, static_cast<uint8_t>((base + i) & 0xFF));
        }

        parasite_thread.join();

        REQUIRE(received.size() == NUM_BYTES);
        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF);
            if (received[i] != expected) {
                FAIL("Iteration " << iter << ", byte " << i
                     << ": expected $" << std::hex << static_cast<int>(expected)
                     << " got $" << std::hex << static_cast<int>(received[i]));
            }
        }
    }
}

// ===========================================================================
// Cross-thread R2 latch ping-pong
// ===========================================================================

TEST_CASE("TubeShared cross-thread: R2 latch ping-pong", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    constexpr int ROUNDS = 100;
    std::vector<uint8_t> parasite_received;
    parasite_received.reserve(ROUNDS);

    std::atomic<bool> parasite_done{false};

    // Parasite thread: read H-to-P, write response to P-to-H
    std::thread parasite([&] {
        for (int i = 0; i < ROUNDS; ++i) {
            // Wait for host to send
            while (shared.r2_h2p.ready.load(std::memory_order_acquire) == 0) {}

            // Read H-to-P
            uint8_t val = parasite_read_latch(shared.r2_h2p);
            parasite_received.push_back(val);

            // Reply with value + 1
            parasite_write_latch(shared.r2_p2h, val + 1);
        }
        parasite_done.store(true, std::memory_order_release);
    });

    // Host thread: write to H-to-P, wait for P-to-H reply
    std::vector<uint8_t> host_received;
    host_received.reserve(ROUNDS);

    for (int i = 0; i < ROUNDS; ++i) {
        uint8_t send_val = static_cast<uint8_t>(i & 0xFF);
        host.host_write(3, send_val);  // R2 data write

        // Wait for parasite reply
        while (shared.r2_p2h.ready.load(std::memory_order_acquire) == 0) {}

        uint8_t reply = host.host_read(3);  // R2 data read
        host_received.push_back(reply);
    }

    parasite.join();

    // Verify parasite received correct values
    REQUIRE(parasite_received.size() == ROUNDS);
    for (int i = 0; i < ROUNDS; ++i) {
        CHECK(parasite_received[i] == static_cast<uint8_t>(i & 0xFF));
    }

    // Verify host received parasite replies (each is sent value + 1)
    REQUIRE(host_received.size() == ROUNDS);
    for (int i = 0; i < ROUNDS; ++i) {
        CHECK(host_received[i] == static_cast<uint8_t>((i + 1) & 0xFF));
    }
}

// ===========================================================================
// Cross-thread R4 latch: bidirectional
// ===========================================================================

TEST_CASE("TubeShared cross-thread: R4 bidirectional latch", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    constexpr int ROUNDS = 50;
    std::vector<uint8_t> parasite_received;
    parasite_received.reserve(ROUNDS);

    std::thread parasite([&] {
        for (int i = 0; i < ROUNDS; ++i) {
            // Wait for host to send via H-to-P
            while (shared.r4_h2p.ready.load(std::memory_order_acquire) == 0) {}

            uint8_t val = parasite_read_latch(shared.r4_h2p);
            parasite_received.push_back(val);

            // Reply via P-to-H with bitwise complement
            parasite_write_latch(shared.r4_p2h, static_cast<uint8_t>(~val));
        }
    });

    std::vector<uint8_t> host_received;
    host_received.reserve(ROUNDS);

    for (int i = 0; i < ROUNDS; ++i) {
        uint8_t send_val = static_cast<uint8_t>(i * 3);
        host.host_write(7, send_val);  // R4 data write

        // Wait for parasite reply
        while (shared.r4_p2h.ready.load(std::memory_order_acquire) == 0) {}

        host_received.push_back(host.host_read(7));  // R4 data read
    }

    parasite.join();

    REQUIRE(parasite_received.size() == ROUNDS);
    REQUIRE(host_received.size() == ROUNDS);

    for (int i = 0; i < ROUNDS; ++i) {
        uint8_t expected_send = static_cast<uint8_t>(i * 3);
        CHECK(parasite_received[i] == expected_send);
        CHECK(host_received[i] == static_cast<uint8_t>(~expected_send));
    }
}

// ===========================================================================
// Cross-thread R3: 2-byte transfer
// ===========================================================================

TEST_CASE("TubeShared cross-thread: R3 two-byte transfer from parasite to host", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();
    TubeHostPort host(&shared);

    // Set V=1 for 2-byte mode
    host.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);

    // Clear the R3 P-to-H dummy byte from init
    shared.r3_p2h.state.store(TubeReg3::pack(0, false), std::memory_order_release);

    std::atomic<bool> parasite_written{false};

    std::thread parasite([&] {
        // Write 2 bytes to R3 P-to-H
        parasite_write_r3(shared.r3_p2h, 0xAA);
        parasite_write_r3(shared.r3_p2h, 0xBB);
        parasite_written.store(true, std::memory_order_release);
    });

    while (!parasite_written.load(std::memory_order_acquire)) {}
    parasite.join();

    // Status should show data available
    uint8_t status = host.host_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read both bytes
    CHECK(host.host_read(5) == 0xAA);
    CHECK(host.host_read(5) == 0xBB);

    // Count should be zero, data no longer available
    CHECK(TubeReg3::count_of(shared.r3_p2h.state.load(std::memory_order_acquire)) == 0);
    status = host.host_read(4);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// Cross-thread lifecycle mailbox
// ===========================================================================

TEST_CASE("TubeShared cross-thread: lifecycle reset handshake", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();

    std::atomic<bool> parasite_done{false};
    TubeLifecycleCommand parasite_saw = TubeLifecycleCommand::None;

    std::thread parasite([&] {
        // Wait for a command
        while (shared.host_command.load(std::memory_order_acquire) == 0) {}

        parasite_saw = static_cast<TubeLifecycleCommand>(
            shared.host_command.load(std::memory_order_acquire));

        // Acknowledge
        shared.parasite_ack.store(
            static_cast<uint8_t>(TubeLifecycleAck::ResetDone),
            std::memory_order_release);
        parasite_done.store(true, std::memory_order_release);
    });

    // Host sends reset
    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Reset),
        std::memory_order_release);

    // Wait for acknowledgement
    while (!parasite_done.load(std::memory_order_acquire)) {}

    auto ack = static_cast<TubeLifecycleAck>(
        shared.parasite_ack.load(std::memory_order_acquire));

    parasite.join();

    CHECK(parasite_saw == TubeLifecycleCommand::Reset);
    CHECK(ack == TubeLifecycleAck::ResetDone);
}

TEST_CASE("TubeShared cross-thread: lifecycle shutdown handshake", "[tube][shared][threads]") {
    TubeShared shared;
    shared.init();

    std::thread parasite([&] {
        while (shared.host_command.load(std::memory_order_acquire) == 0) {}

        shared.parasite_ack.store(
            static_cast<uint8_t>(TubeLifecycleAck::Exiting),
            std::memory_order_release);
    });

    shared.host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Shutdown),
        std::memory_order_release);

    // Wait for ack
    while (shared.parasite_ack.load(std::memory_order_acquire) == 0) {}

    auto ack = static_cast<TubeLifecycleAck>(
        shared.parasite_ack.load(std::memory_order_acquire));

    parasite.join();

    CHECK(ack == TubeLifecycleAck::Exiting);
}
