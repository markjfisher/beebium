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

// Cross-process tests for Tube R1 data transfer via POSIX shared memory.
//
// These tests fork a child process to act as the parasite, communicating
// with the parent (host) through a TubeSharedMemory region. This exercises
// the actual multi-process shared memory path used by the emulator,
// catching issues that multi-thread tests cannot (e.g., mmap caching
// attributes, cross-process atomic visibility).

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeHostPort.hpp>
#include <beebium/tube/TubeParasitePort.hpp>
#include <beebium/tube/TubeSharedMemory.hpp>
#include <beebium/tube/TubeUla.hpp>
#include <beebium/PlatformUtils.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace beebium;

static std::string unique_suffix() {
    static int counter = 0;
    return "xproc_" + std::to_string(platform::get_pid()) + "_" + std::to_string(counter++);
}

// ===========================================================================
// Cross-process R1 H-to-P latch transfer
// ===========================================================================

TEST_CASE("Cross-process R1 H-to-P: single byte", "[tube][crossprocess]") {
    auto suffix = unique_suffix();
    TubeSharedMemory host_shm(suffix, TubeSharedMemoryRole::Creator);
    TubeHostPort host(host_shm.get());

    pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        // Child process: parasite
        TubeSharedMemory parasite_shm(suffix, TubeSharedMemoryRole::Joiner);
        TubeParasitePort parasite(parasite_shm.get());

        // Poll until data available
        while ((parasite.parasite_read(0) & TubeUla::DATA_AVAILABLE) == 0) {}
        uint8_t byte = parasite.parasite_read(1);
        _exit(byte == 0xAB ? 0 : 1);
    }

    // Parent: host writes one byte
    host.host_write(1, 0xAB);

    int status = 0;
    waitpid(pid, &status, 0);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("Cross-process R1 H-to-P: 702-byte sustained transfer", "[tube][crossprocess]") {
    // Simulates the CE2023 R1 polled transfer across process boundaries.
    auto suffix = unique_suffix();
    TubeSharedMemory host_shm(suffix, TubeSharedMemoryRole::Creator);
    TubeHostPort host(host_shm.get());

    constexpr int NUM_BYTES = 702;

    pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        // Child: parasite reads NUM_BYTES via polled R1
        TubeSharedMemory parasite_shm(suffix, TubeSharedMemoryRole::Joiner);
        TubeParasitePort parasite(parasite_shm.get());

        for (int i = 0; i < NUM_BYTES; ++i) {
            // Poll status until data available
            while ((parasite.parasite_read(0) & TubeUla::DATA_AVAILABLE) == 0) {}
            uint8_t byte = parasite.parasite_read(1);
            uint8_t expected = static_cast<uint8_t>(i & 0xFF);
            if (byte != expected) {
                // Report the first mismatch via exit code (non-zero)
                _exit(i + 1);  // exit code = 1-based index of first bad byte
            }
        }
        _exit(0);  // all bytes correct
    }

    // Parent: host writes NUM_BYTES via bus-stretched R1
    for (int i = 0; i < NUM_BYTES; ++i) {
        host.host_write(1, static_cast<uint8_t>(i & 0xFF));
    }

    int status = 0;
    waitpid(pid, &status, 0);
    REQUIRE(WIFEXITED(status));
    if (WEXITSTATUS(status) != 0) {
        FAIL("First corrupted byte at index " << (WEXITSTATUS(status) - 1));
    }
}

TEST_CASE("Cross-process R1 H-to-P: repeated 702-byte stress", "[tube][crossprocess]") {
    // Run the transfer 50 times to catch non-deterministic races.
    constexpr int NUM_BYTES = 702;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        auto suffix = unique_suffix();
        TubeSharedMemory host_shm(suffix, TubeSharedMemoryRole::Creator);
        TubeHostPort host(host_shm.get());

        uint8_t base = static_cast<uint8_t>(iter * 7);

        pid_t pid = fork();
        REQUIRE(pid >= 0);

        if (pid == 0) {
            TubeSharedMemory parasite_shm(suffix, TubeSharedMemoryRole::Joiner);
            TubeParasitePort parasite(parasite_shm.get());

            for (int i = 0; i < NUM_BYTES; ++i) {
                while ((parasite.parasite_read(0) & TubeUla::DATA_AVAILABLE) == 0) {}
                uint8_t byte = parasite.parasite_read(1);
                uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF);
                if (byte != expected) {
                    _exit(1);
                }
            }
            _exit(0);
        }

        for (int i = 0; i < NUM_BYTES; ++i) {
            host.host_write(1, static_cast<uint8_t>((base + i) & 0xFF));
        }

        int status = 0;
        waitpid(pid, &status, 0);
        REQUIRE(WIFEXITED(status));
        if (WEXITSTATUS(status) != 0) {
            FAIL("Iteration " << iter << ": byte corruption detected");
        }
    }
}
