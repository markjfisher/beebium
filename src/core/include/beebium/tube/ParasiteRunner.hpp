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

#pragma once

#include "ParasiteCpu.hpp"
#include "ParasiteMemoryMap.hpp"
#include "TubeParasitePort.hpp"
#include "TubeShared.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>

namespace beebium {

// Parasite execution runner -- the emulation engine for a second processor.
//
// Owns the CPU, memory map, and Tube port, and provides an execution loop
// with lifecycle handling via the TubeShared mailbox. This is the parasite's
// analogue of Machine<Hardware> on the host side.
//
// The runner periodically polls the lifecycle mailbox for commands from the
// host (reset, freeze, shutdown) and responds accordingly. It also supports
// pause/resume for debugger integration, following the same pattern as the
// host Machine's wait_if_paused()/request_shutdown().
//
// The mailbox is polled every `mailbox_poll_interval` cycles (default 1024)
// to amortise the cost of the atomic load.
//
// This class is specific to the 6502 second processor family. Future
// coprocessors (6809, Z80, 80186, 32016) would have their own runner
// classes with different CPU and memory map types.

class ParasiteRunner {
public:
    // Construct with a pointer to the shared memory region and a 2 KB ROM image.
    ParasiteRunner(TubeShared* shared, std::span<const uint8_t, 2048> rom);
    ~ParasiteRunner() = default;

    // Non-copyable (owns M6502 with internal pointers)
    ParasiteRunner(const ParasiteRunner&) = delete;
    ParasiteRunner& operator=(const ParasiteRunner&) = delete;

    // Reset CPU, memory map, and Tube port. Clears cycle count.
    void reset();

    // Execute for the given number of cycles, or until shutdown/freeze.
    // Polls the lifecycle mailbox and checks pause state periodically.
    void run(uint64_t cycles);

    // Execute one complete instruction. Returns the number of cycles taken.
    uint64_t step_instruction();

    // Cycle counter.
    uint64_t cycle_count() const { return cpu_.cycle_count(); }

    // --- Debugger pause/resume ---

    void pause();
    void resume();
    bool is_paused() const { return paused_.load(std::memory_order_acquire); }

    // Request clean shutdown. Unblocks wait_if_paused() and freeze waits.
    void request_shutdown();
    bool shutdown_requested() const { return shutdown_requested_.load(std::memory_order_acquire); }

    // --- Component access ---

    M6502& cpu() { return cpu_.cpu(); }
    const M6502& cpu() const { return cpu_.cpu(); }

    ParasiteMemoryMap& memory_map() { return memory_; }
    const ParasiteMemoryMap& memory_map() const { return memory_; }

    TubeParasitePort& tube_port() { return tube_port_; }
    const TubeParasitePort& tube_port() const { return tube_port_; }

private:
    // How often to poll the lifecycle mailbox (in CPU cycles).
    static constexpr uint64_t mailbox_poll_interval = 1024;

    // Poll the lifecycle mailbox and act on any pending command.
    // Returns true if execution should continue, false if it should stop.
    bool poll_mailbox();

    // Block while paused. Returns false if shutdown was requested during wait.
    bool wait_if_paused();

    // Block while frozen. Returns false if shutdown was requested.
    bool wait_while_frozen();

    TubeShared* shared_;
    TubeParasitePort tube_port_;
    ParasiteMemoryMap memory_;
    ParasiteCpu cpu_;

    // ROM image (kept for reset)
    std::array<uint8_t, 2048> rom_;

    // Debugger pause state
    mutable std::mutex pause_mutex_;
    std::condition_variable pause_cv_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> shutdown_requested_{false};
};

}  // namespace beebium
