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

// Regression test for the R3 register race condition.
//
// The R3 2-byte register uses a shift-down design. These tests stream bytes
// through R3 H-to-P and P-to-H using host_write/host_read and
// parasite_write/parasite_read on separate threads, verifying that every
// byte arrives in order. A single lost byte causes the consumer to hang,
// which the test detects via a timeout.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeUla.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace beebium;

// ---------------------------------------------------------------------------
// R3 H-to-P: host writes via host_write, parasite reads via parasite_read
// ---------------------------------------------------------------------------

TEST_CASE("R3 H-to-P concurrent transfer loses no bytes", "[tube][r3][race]") {
    // This test reproduces the race condition in the R3 shift register.
    // The host writes 256 bytes to R3 (offset 5) via host_write(), which
    // uses bus stretching (blocks while count >= 2). The parasite reads
    // R3 data (offset 5) via parasite_read(), which dequeues and shifts.
    //
    // On a buggy implementation, lost updates would cause the parasite to
    // block forever waiting for data that was silently dropped.

    constexpr int NUM_BYTES = 256;
    constexpr auto TIMEOUT = std::chrono::seconds(5);

    TubeUla tube;

    // Drain the dummy P-to-H byte that TubeUla::reset() seeds.
    tube.host_read(5);

    std::vector<uint8_t> received;
    received.reserve(NUM_BYTES);
    std::atomic<bool> parasite_done{false};
    std::atomic<bool> parasite_timed_out{false};

    // Parasite thread: poll R3 status then read R3 data, like the real
    // Tube Client ROM's 256-byte transfer loop:
    //   .loop  LDA $FEFC : AND #$80 : BPL loop  ; poll R3 status
    //          LDA $FEFD                          ; read R3 data
    //          STA (dest),Y : INY : BNE loop
    std::thread parasite_thread([&] {
        auto deadline = std::chrono::steady_clock::now() + TIMEOUT;

        for (int i = 0; i < NUM_BYTES; ++i) {
            // Poll R3 status (offset 4) until DATA_AVAILABLE (bit 7).
            while (true) {
                uint8_t status = tube.parasite_read(4);
                if (status & TubeUla::DATA_AVAILABLE)
                    break;

                if (std::chrono::steady_clock::now() > deadline) {
                    parasite_timed_out.store(true, std::memory_order_release);
                    parasite_done.store(true, std::memory_order_release);
                    return;
                }
            }

            // Read R3 data (offset 5).
            uint8_t byte = tube.parasite_read(5);
            received.push_back(byte);
        }

        parasite_done.store(true, std::memory_order_release);
    });

    // Host thread: write all bytes to R3 (offset 5). Poll for space
    // before each write, as TubeUla buffers at most one pending write
    // rather than spin-waiting.
    for (int i = 0; i < NUM_BYTES; ++i) {
        while ((tube.host_peek(4) & TubeUla::SPACE_AVAILABLE) == 0) {}
        tube.host_write(5, static_cast<uint8_t>(i));
    }

    // Wait for the parasite to finish (or timeout).
    parasite_thread.join();

    // Diagnose failure mode.
    if (parasite_timed_out.load(std::memory_order_acquire)) {
        INFO("Parasite timed out after receiving " << received.size()
             << " of " << NUM_BYTES << " bytes");
        FAIL("Parasite timed out -- likely a lost byte due to the R3 count race");
    }

    // Verify every byte arrived in order.
    REQUIRE(received.size() == NUM_BYTES);
    for (int i = 0; i < NUM_BYTES; ++i) {
        CHECK(received[i] == static_cast<uint8_t>(i));
    }

    // R3 H2P should have space available (empty).
    CHECK((tube.host_peek(4) & TubeUla::SPACE_AVAILABLE) != 0);
}

// ---------------------------------------------------------------------------
// Same test for R3 P-to-H direction (parasite writes, host reads)
// ---------------------------------------------------------------------------

TEST_CASE("R3 P-to-H concurrent transfer loses no bytes", "[tube][r3][race]") {
    constexpr int NUM_BYTES = 256;
    constexpr auto TIMEOUT = std::chrono::seconds(5);

    TubeUla tube;

    // Drain the dummy P-to-H byte that TubeUla::reset() seeds.
    tube.host_read(5);

    std::vector<uint8_t> received;
    received.reserve(NUM_BYTES);
    std::atomic<bool> host_timed_out{false};

    // Parasite thread: write bytes to R3 P-to-H (offset 5) via parasite_write.
    std::thread parasite_thread([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            // Poll R3 status (offset 4) until SPACE_AVAILABLE (bit 6).
            while ((tube.parasite_peek(4) & TubeUla::SPACE_AVAILABLE) == 0) {}
            tube.parasite_write(5, static_cast<uint8_t>(i));
        }
    });

    // Host thread: read bytes from R3 P-to-H via host_read.
    auto deadline = std::chrono::steady_clock::now() + TIMEOUT;

    for (int i = 0; i < NUM_BYTES; ++i) {
        // Poll R3 status (offset 4) until DATA_AVAILABLE.
        while (true) {
            uint8_t status = tube.host_read(4);
            if (status & TubeUla::DATA_AVAILABLE)
                break;

            if (std::chrono::steady_clock::now() > deadline) {
                host_timed_out.store(true, std::memory_order_release);
                break;
            }
        }

        if (host_timed_out.load(std::memory_order_acquire))
            break;

        uint8_t byte = tube.host_read(5);
        received.push_back(byte);
    }

    parasite_thread.join();

    if (host_timed_out.load(std::memory_order_acquire)) {
        INFO("Host timed out after receiving " << received.size()
             << " of " << NUM_BYTES << " bytes");
        FAIL("Host timed out -- likely a lost byte due to the R3 count race");
    }

    REQUIRE(received.size() == NUM_BYTES);
    for (int i = 0; i < NUM_BYTES; ++i) {
        CHECK(received[i] == static_cast<uint8_t>(i));
    }

    // R3 P2H should be empty (no data available from host perspective).
    CHECK((tube.host_peek(4) & TubeUla::DATA_AVAILABLE) == 0);
}

// ---------------------------------------------------------------------------
// Stress test: multiple 256-byte blocks (like loading a multi-page file)
// ---------------------------------------------------------------------------

TEST_CASE("R3 H-to-P concurrent multi-block transfer", "[tube][r3][race]") {
    constexpr int NUM_BLOCKS = 20;
    constexpr int BLOCK_SIZE = 256;
    constexpr int TOTAL_BYTES = NUM_BLOCKS * BLOCK_SIZE;
    constexpr auto TIMEOUT = std::chrono::seconds(10);

    TubeUla tube;

    // Drain the dummy P-to-H byte that TubeUla::reset() seeds.
    tube.host_read(5);

    std::vector<uint8_t> received;
    received.reserve(TOTAL_BYTES);
    std::atomic<bool> parasite_timed_out{false};

    std::thread parasite_thread([&] {
        auto deadline = std::chrono::steady_clock::now() + TIMEOUT;

        for (int i = 0; i < TOTAL_BYTES; ++i) {
            while (true) {
                uint8_t status = tube.parasite_read(4);
                if (status & TubeUla::DATA_AVAILABLE)
                    break;

                if (std::chrono::steady_clock::now() > deadline) {
                    parasite_timed_out.store(true, std::memory_order_release);
                    return;
                }
            }

            uint8_t byte = tube.parasite_read(5);
            received.push_back(byte);
        }
    });

    for (int i = 0; i < TOTAL_BYTES; ++i) {
        while ((tube.host_peek(4) & TubeUla::SPACE_AVAILABLE) == 0) {}
        tube.host_write(5, static_cast<uint8_t>(i & 0xFF));
    }

    parasite_thread.join();

    if (parasite_timed_out.load(std::memory_order_acquire)) {
        INFO("Parasite timed out after receiving " << received.size()
             << " of " << TOTAL_BYTES << " bytes ("
             << received.size() / BLOCK_SIZE << " complete blocks)");
        FAIL("Parasite timed out during multi-block transfer");
    }

    REQUIRE(received.size() == TOTAL_BYTES);
    for (int i = 0; i < TOTAL_BYTES; ++i) {
        CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
    }
}
