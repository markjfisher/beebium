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

// Tests for TubeSharedMemory -- POSIX shared memory RAII manager.
//
// These tests create and join shared memory regions within a single process.
// Cross-process correctness is guaranteed by the POSIX shm_open/mmap semantics
// and the atomic ordering in TubeShared.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeSharedMemory.hpp>

#include <beebium/PlatformUtils.hpp>

#include <string>
#include <thread>

using namespace beebium;

// Generate a unique suffix for each test to avoid collisions.
static std::string unique_suffix() {
    static int counter = 0;
    return "test_" + std::to_string(platform::get_pid()) + "_" + std::to_string(counter++);
}

// ===========================================================================
// Creator basics
// ===========================================================================

TEST_CASE("TubeSharedMemory: creator produces valid region", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

    CHECK(shm.get() != nullptr);
    CHECK(shm.get()->valid());
    CHECK(shm.role() == TubeSharedMemoryRole::Creator);
    CHECK(shm.size() >= sizeof(TubeShared));
}

TEST_CASE("TubeSharedMemory: creator initialises header fields", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

    CHECK(shm->magic == TUBE_SHARED_MAGIC);
    CHECK(shm->version == TUBE_SHARED_VERSION);
}

TEST_CASE("TubeSharedMemory: creator initialises registers to zero", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

    CHECK(shm->control_flags.load(std::memory_order_acquire) == 0);
    CHECK(shm->r1_h2p.ready.load(std::memory_order_acquire) == 0);
    CHECK(shm->r1_p2h.count.load(std::memory_order_acquire) == 0);
    CHECK(shm->host_command.load(std::memory_order_acquire) == 0);
    CHECK(shm->parasite_ack.load(std::memory_order_acquire) == 0);
}

TEST_CASE("TubeSharedMemory: name includes suffix", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

    CHECK(shm.name().find(suffix) != std::string::npos);
}

TEST_CASE("TubeSharedMemory: size is page-aligned", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

    size_t ps = beebium::platform::get_page_size();
    REQUIRE(ps > 0);
    CHECK((shm.size() % ps) == 0);
}

// ===========================================================================
// Joiner basics
// ===========================================================================

TEST_CASE("TubeSharedMemory: joiner opens existing region", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory creator(suffix, TubeSharedMemoryRole::Creator);
    TubeSharedMemory joiner(suffix, TubeSharedMemoryRole::Joiner);

    CHECK(joiner.get() != nullptr);
    CHECK(joiner.get()->valid());
    CHECK(joiner.role() == TubeSharedMemoryRole::Joiner);
}

TEST_CASE("TubeSharedMemory: joiner sees creator's initialisation", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory creator(suffix, TubeSharedMemoryRole::Creator);

    // Creator writes some data
    creator->control_flags.store(0x07, std::memory_order_release);
    creator->r1_h2p.value.store(0xAB, std::memory_order_relaxed);
    creator->r1_h2p.ready.store(1, std::memory_order_release);

    TubeSharedMemory joiner(suffix, TubeSharedMemoryRole::Joiner);

    // Joiner sees the data
    CHECK(joiner->control_flags.load(std::memory_order_acquire) == 0x07);
    CHECK(joiner->r1_h2p.value.load(std::memory_order_acquire) == 0xAB);
    CHECK(joiner->r1_h2p.ready.load(std::memory_order_acquire) == 1);
}

TEST_CASE("TubeSharedMemory: joiner fails on nonexistent region", "[tube][shm]") {
    auto suffix = unique_suffix();
    CHECK_THROWS_AS(
        TubeSharedMemory(suffix, TubeSharedMemoryRole::Joiner),
        std::runtime_error);
}

// ===========================================================================
// Cross-mapping visibility
// ===========================================================================

TEST_CASE("TubeSharedMemory: writes through one mapping visible in the other", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory creator(suffix, TubeSharedMemoryRole::Creator);
    TubeSharedMemory joiner(suffix, TubeSharedMemoryRole::Joiner);

    // Both point to the same physical memory (different virtual addresses)
    REQUIRE(creator.get() != joiner.get());

    // Creator writes, joiner reads
    creator->r2_h2p.value.store(0xCD, std::memory_order_relaxed);
    creator->r2_h2p.ready.store(1, std::memory_order_release);

    CHECK(joiner->r2_h2p.ready.load(std::memory_order_acquire) == 1);
    CHECK(joiner->r2_h2p.value.load(std::memory_order_relaxed) == 0xCD);

    // Joiner writes, creator reads
    joiner->r2_p2h.value.store(0xEF, std::memory_order_relaxed);
    joiner->r2_p2h.ready.store(1, std::memory_order_release);

    CHECK(creator->r2_p2h.ready.load(std::memory_order_acquire) == 1);
    CHECK(creator->r2_p2h.value.load(std::memory_order_relaxed) == 0xEF);
}

// ===========================================================================
// Cross-thread with separate mappings
// ===========================================================================

TEST_CASE("TubeSharedMemory: cross-thread communication via separate mappings", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory creator(suffix, TubeSharedMemoryRole::Creator);

    std::atomic<bool> writer_done{false};

    std::thread writer([&suffix, &writer_done] {
        TubeSharedMemory joiner(suffix, TubeSharedMemoryRole::Joiner);

        // Simulate parasite writing banner bytes to R1 P-to-H FIFO
        for (uint8_t i = 0; i < 10; ++i) {
            uint8_t tail = joiner->r1_p2h.tail.load(std::memory_order_acquire);
            joiner->r1_p2h.data[tail].store(0x40 + i, std::memory_order_relaxed);
            joiner->r1_p2h.tail.store((tail + 1) % 24, std::memory_order_relaxed);
            joiner->r1_p2h.count.store(i + 1, std::memory_order_release);
        }

        writer_done.store(true, std::memory_order_release);
    });

    writer.join();

    // Creator (host) reads the data
    REQUIRE(writer_done.load(std::memory_order_acquire));
    CHECK(creator->r1_p2h.count.load(std::memory_order_acquire) == 10);

    uint8_t head = creator->r1_p2h.head.load(std::memory_order_acquire);
    for (uint8_t i = 0; i < 10; ++i) {
        uint8_t byte = creator->r1_p2h.data[(head + i) % 24]
            .load(std::memory_order_relaxed);
        CHECK(byte == 0x40 + i);
    }
}

// ===========================================================================
// Lifecycle mailbox through shared memory
// ===========================================================================

TEST_CASE("TubeSharedMemory: lifecycle mailbox round-trip", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory creator(suffix, TubeSharedMemoryRole::Creator);
    TubeSharedMemory joiner(suffix, TubeSharedMemoryRole::Joiner);

    // Host sends reset command
    creator->host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Reset),
        std::memory_order_release);

    // Parasite sees it
    auto cmd = static_cast<TubeLifecycleCommand>(
        joiner->host_command.load(std::memory_order_acquire));
    CHECK(cmd == TubeLifecycleCommand::Reset);

    // Parasite acknowledges
    joiner->parasite_ack.store(
        static_cast<uint8_t>(TubeLifecycleAck::ResetDone),
        std::memory_order_release);

    // Host sees acknowledgement
    auto ack = static_cast<TubeLifecycleAck>(
        creator->parasite_ack.load(std::memory_order_acquire));
    CHECK(ack == TubeLifecycleAck::ResetDone);
}

// ===========================================================================
// Duplicate creation fails
// ===========================================================================

TEST_CASE("TubeSharedMemory: duplicate creator throws", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory first(suffix, TubeSharedMemoryRole::Creator);

    CHECK_THROWS_AS(
        TubeSharedMemory(suffix, TubeSharedMemoryRole::Creator),
        std::runtime_error);
}

// ===========================================================================
// Cleanup
// ===========================================================================

TEST_CASE("TubeSharedMemory: creator unlinks on destruction", "[tube][shm]") {
    auto suffix = unique_suffix();
    std::string name;

    {
        TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);
        name = shm.name();
    }
    // After destruction, the name should be unlinked.
    // A joiner attempting to open it should fail.
    CHECK_THROWS_AS(
        TubeSharedMemory(suffix, TubeSharedMemoryRole::Joiner),
        std::runtime_error);
}

TEST_CASE("TubeSharedMemory: joiner does not unlink on destruction", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory creator(suffix, TubeSharedMemoryRole::Creator);

    {
        TubeSharedMemory joiner(suffix, TubeSharedMemoryRole::Joiner);
        // Joiner destructs here
    }

    // Creator's region is still accessible -- another joiner can open it
    TubeSharedMemory joiner2(suffix, TubeSharedMemoryRole::Joiner);
    CHECK(joiner2.get()->valid());
}

// ===========================================================================
// Smart pointer interface
// ===========================================================================

TEST_CASE("TubeSharedMemory: operator-> and operator* access", "[tube][shm]") {
    auto suffix = unique_suffix();
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

    // operator-> accesses members
    shm->control_flags.store(0x05, std::memory_order_release);
    CHECK(shm->control_flags.load(std::memory_order_acquire) == 0x05);

    // operator* returns reference
    TubeShared& ref = *shm;
    CHECK(ref.valid());
}

// ===========================================================================
// macOS shm_open name length limit
// ===========================================================================

// macOS limits shm_open names to 30 characters (PSHMNAMLEN - 1, excluding
// the mandatory leading '/'). This test verifies that a suffix matching the
// length produced by ServerMain's UUID-based generation stays within limits.
TEST_CASE("TubeSharedMemory: 29-char suffix fits macOS shm_open limit", "[tube][shm]") {
    // Simulate ServerMain: "tube_" (5) + 24 hex chars from stripped UUID = 29
    std::string suffix = "tube_" + std::string(24, 'a');
    TubeSharedMemory shm(suffix, TubeSharedMemoryRole::Creator);

#ifndef _WIN32
    // POSIX name is "/<suffix>", so 30 chars total (within macOS limit)
    CHECK(shm.name() == "/" + suffix);
    CHECK(shm.name().size() == 30);
#endif
    CHECK(shm.get() != nullptr);
    CHECK(shm.get()->valid());
}

TEST_CASE("TubeSharedMemory: 31-char suffix exceeds macOS shm_open limit", "[tube][shm]") {
    // PSHMNAMLEN is 31 on macOS, so the full name (including '/') can be at
    // most 31 chars. A 31-char suffix makes a 32-char name: too long.
    std::string suffix = "tube_" + std::string(26, 'b');

#ifdef __APPLE__
    CHECK_THROWS(TubeSharedMemory(suffix, TubeSharedMemoryRole::Creator));
#endif
}
