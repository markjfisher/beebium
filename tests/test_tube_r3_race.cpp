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
// The R3 2-byte register uses a shift-down design with a non-atomic
// load-calculate-store on count. When the host writes (incrementing count)
// and the parasite reads (decrementing count) concurrently, the last-writer
// wins and one side's update is lost. This causes bytes to vanish from the
// FIFO, leading to deadlocks in real Tube transfers (e.g. 256-byte blocks).
//
// This test streams bytes through R3 H-to-P using TubeHostPort::host_write
// and TubeParasitePort::parasite_read on separate threads, verifying that
// every byte arrives in order. A single lost byte causes the consumer to
// hang, which the test detects via a timeout.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeShared.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace beebium;

// ---------------------------------------------------------------------------
// R3 H-to-P: host writes via TubeHostPort, parasite reads via TubeParasitePort
// ---------------------------------------------------------------------------

TEST_CASE("R3 H-to-P concurrent transfer loses no bytes", "[tube][r3][race]") {
    // This test reproduces the race condition in the R3 shift register.
    // The host writes 256 bytes to R3 (offset 5) via host_write(), which
    // uses bus stretching (spin while count >= 2). The parasite reads
    // R3 data (offset 5) via parasite_read(), which dequeues and shifts.
    //
    // On the buggy implementation, the non-atomic load-store on count
    // causes lost updates when both sides run concurrently. A lost byte
    // means the parasite blocks forever waiting for data that was silently
    // dropped.

    constexpr int NUM_BYTES = 256;
    constexpr auto TIMEOUT = std::chrono::seconds(5);

    TubeShared shared;
    shared.init();

    // V=0 selects 1-byte threshold mode (the mode used by 256-byte block transfers).
    // With threshold=1, each byte triggers DATA_AVAILABLE individually.
    shared.control_flags.store(0, std::memory_order_release);

    // Clear R3 H-to-P to a known empty state.
    shared.r3_h2p.data[0].store(0, std::memory_order_relaxed);
    shared.r3_h2p.data[1].store(0, std::memory_order_relaxed);
    shared.r3_h2p.count.store(0, std::memory_order_release);

    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

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
                uint8_t status = parasite.parasite_read(4);
                if (status & TubeUla::DATA_AVAILABLE)
                    break;

                if (std::chrono::steady_clock::now() > deadline) {
                    parasite_timed_out.store(true, std::memory_order_release);
                    parasite_done.store(true, std::memory_order_release);
                    return;
                }
            }

            // Read R3 data (offset 5).
            uint8_t byte = parasite.parasite_read(5);
            received.push_back(byte);
        }

        parasite_done.store(true, std::memory_order_release);
    });

    // Host thread: write all bytes to R3 (offset 5). The bus stretching
    // spin inside host_write blocks while count >= 2, so this naturally
    // pushes bytes as fast as the parasite consumes them.
    for (int i = 0; i < NUM_BYTES; ++i) {
        host.host_write(5, static_cast<uint8_t>(i));
    }

    // Wait for the parasite to finish (or timeout).
    parasite_thread.join();

    // Diagnose failure mode.
    if (parasite_timed_out.load(std::memory_order_acquire)) {
        INFO("Parasite timed out after receiving " << received.size()
             << " of " << NUM_BYTES << " bytes");
        INFO("R3 H-to-P count: "
             << static_cast<int>(shared.r3_h2p.count.load(std::memory_order_acquire)));
        FAIL("Parasite timed out -- likely a lost byte due to the R3 count race");
    }

    // Verify every byte arrived in order.
    REQUIRE(received.size() == NUM_BYTES);
    for (int i = 0; i < NUM_BYTES; ++i) {
        CHECK(received[i] == static_cast<uint8_t>(i));
    }

    // FIFO should be empty.
    CHECK(shared.r3_h2p.count.load(std::memory_order_acquire) == 0);
}

// ---------------------------------------------------------------------------
// Same test for R3 P-to-H direction (parasite writes, host reads)
// ---------------------------------------------------------------------------

TEST_CASE("R3 P-to-H concurrent transfer loses no bytes", "[tube][r3][race]") {
    constexpr int NUM_BYTES = 256;
    constexpr auto TIMEOUT = std::chrono::seconds(5);

    TubeShared shared;
    shared.init();

    // V=0 selects 1-byte threshold mode (the mode used by 256-byte block transfers).
    shared.control_flags.store(0, std::memory_order_release);

    // Clear R3 P-to-H (init places a dummy byte to prevent spurious PNMI).
    shared.r3_p2h.data[0].store(0, std::memory_order_relaxed);
    shared.r3_p2h.data[1].store(0, std::memory_order_relaxed);
    shared.r3_p2h.count.store(0, std::memory_order_release);

    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    std::vector<uint8_t> received;
    received.reserve(NUM_BYTES);
    std::atomic<bool> host_timed_out{false};

    // Parasite thread: write bytes to R3 P-to-H (offset 5) via parasite_write.
    std::thread parasite_thread([&] {
        for (int i = 0; i < NUM_BYTES; ++i) {
            // parasite_write to R3 spins internally if count >= 2 (enqueue_r3_p2h
            // drops bytes if full, so we must poll).
            while (true) {
                uint8_t count = shared.r3_p2h.count.load(std::memory_order_acquire);
                if (count < 2) {
                    parasite.parasite_write(5, static_cast<uint8_t>(i));
                    break;
                }
            }
        }
    });

    // Host thread: read bytes from R3 P-to-H via host_read.
    auto deadline = std::chrono::steady_clock::now() + TIMEOUT;

    for (int i = 0; i < NUM_BYTES; ++i) {
        // Poll R3 status (offset 4) until DATA_AVAILABLE.
        while (true) {
            uint8_t status = host.host_read(4);
            if (status & TubeUla::DATA_AVAILABLE)
                break;

            if (std::chrono::steady_clock::now() > deadline) {
                host_timed_out.store(true, std::memory_order_release);
                break;
            }
        }

        if (host_timed_out.load(std::memory_order_acquire))
            break;

        uint8_t byte = host.host_read(5);
        received.push_back(byte);
    }

    parasite_thread.join();

    if (host_timed_out.load(std::memory_order_acquire)) {
        INFO("Host timed out after receiving " << received.size()
             << " of " << NUM_BYTES << " bytes");
        INFO("R3 P-to-H count: "
             << static_cast<int>(shared.r3_p2h.count.load(std::memory_order_acquire)));
        FAIL("Host timed out -- likely a lost byte due to the R3 count race");
    }

    REQUIRE(received.size() == NUM_BYTES);
    for (int i = 0; i < NUM_BYTES; ++i) {
        CHECK(received[i] == static_cast<uint8_t>(i));
    }

    CHECK(shared.r3_p2h.count.load(std::memory_order_acquire) == 0);
}

// ---------------------------------------------------------------------------
// Stress test: multiple 256-byte blocks (like loading a multi-page file)
// ---------------------------------------------------------------------------

TEST_CASE("R3 H-to-P concurrent multi-block transfer", "[tube][r3][race]") {
    constexpr int NUM_BLOCKS = 20;
    constexpr int BLOCK_SIZE = 256;
    constexpr int TOTAL_BYTES = NUM_BLOCKS * BLOCK_SIZE;
    constexpr auto TIMEOUT = std::chrono::seconds(10);

    TubeShared shared;
    shared.init();

    // V=0 selects 1-byte threshold mode (the mode used by 256-byte block transfers).
    shared.control_flags.store(0, std::memory_order_release);

    shared.r3_h2p.data[0].store(0, std::memory_order_relaxed);
    shared.r3_h2p.data[1].store(0, std::memory_order_relaxed);
    shared.r3_h2p.count.store(0, std::memory_order_release);

    TubeHostPort host(&shared);
    TubeParasitePort parasite(&shared);

    std::vector<uint8_t> received;
    received.reserve(TOTAL_BYTES);
    std::atomic<bool> parasite_timed_out{false};

    std::thread parasite_thread([&] {
        auto deadline = std::chrono::steady_clock::now() + TIMEOUT;

        for (int i = 0; i < TOTAL_BYTES; ++i) {
            while (true) {
                uint8_t status = parasite.parasite_read(4);
                if (status & TubeUla::DATA_AVAILABLE)
                    break;

                if (std::chrono::steady_clock::now() > deadline) {
                    parasite_timed_out.store(true, std::memory_order_release);
                    return;
                }
            }

            uint8_t byte = parasite.parasite_read(5);
            received.push_back(byte);
        }
    });

    for (int i = 0; i < TOTAL_BYTES; ++i) {
        host.host_write(5, static_cast<uint8_t>(i & 0xFF));
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
