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
#include "../Types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

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
    using Memory = ParasiteMemoryMap;
    using BreakpointHitCallback = std::function<void(const BreakpointEntry& bp, uint16_t pc)>;
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
    void prepare_for_step() {} // No bus stretching on parasite side

    // Wait until run() has exited after a pause.
    void wait_until_idle() {
        while (in_run_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    // Request clean shutdown. Unblocks wait_if_paused() and freeze waits.
    void request_shutdown();
    bool shutdown_requested() const { return shutdown_requested_.load(std::memory_order_acquire); }

    // --- Sequence counter (increments on mutations, for change detection) ---

    uint64_t sequence() const { return sequence_.load(std::memory_order_acquire); }

    // --- CPU register accessors (debugger convenience) ---

    uint8_t a() const { return cpu_.cpu().a; }
    uint8_t x() const { return cpu_.cpu().x; }
    uint8_t y() const { return cpu_.cpu().y; }
    uint8_t sp() const { return cpu_.cpu().s.b.l; }
    uint16_t pc() const { return cpu_.cpu().opcode_pc.w; }
    uint8_t p() const { return cpu_.cpu().p.value; }

    // Interrupt handler tracking
    bool in_nmi_handler() const { return cpu_.in_nmi_handler(); }
    bool in_irq_handler() const { return false; }

    void set_a(uint8_t value) { cpu_.cpu().a = value; ++sequence_; }
    void set_x(uint8_t value) { cpu_.cpu().x = value; ++sequence_; }
    void set_y(uint8_t value) { cpu_.cpu().y = value; ++sequence_; }
    void set_sp(uint8_t value) { cpu_.cpu().s.b.l = value; ++sequence_; }
    void set_pc(uint16_t value) {
        cpu_.cpu().opcode_pc.w = value;
        cpu_.cpu().pc.w = value + 1;
        cpu_.cpu().dbus = memory_.peek(value);
        ++sequence_;
    }
    void set_p(uint8_t value) { cpu_.cpu().p.value = value; ++sequence_; }

    // --- Memory access ---

    uint8_t read(uint16_t addr) { return memory_.read(addr); }
    void write(uint16_t addr, uint8_t value) { memory_.write(addr, value); ++sequence_; }
    uint8_t peek(uint16_t addr) const { return memory_.peek(addr); }

    ParasiteMemoryMap& memory() { return memory_; }
    const ParasiteMemoryMap& memory() const { return memory_; }

    // --- Single-cycle step ---

    void step();

    // --- Breakpoint management (sorted vector, modified only while stopped) ---

    void set_breakpoint_entries(std::vector<BreakpointEntry> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const BreakpointEntry& a, const BreakpointEntry& b) {
                      return a.start < b.start;
                  });
        breakpoint_entries_ = std::move(entries);
    }

    const std::vector<BreakpointEntry>& breakpoint_entries() const { return breakpoint_entries_; }

    void set_breakpoint_hit_callback(BreakpointHitCallback cb) {
        on_breakpoint_hit_ = std::move(cb);
    }

    // --- Watchpoint management (sorted by start, modified only while stopped) ---

    using WatchpointHitCallback = std::function<void(const WatchpointEntry& wp, uint16_t addr, uint8_t value, bool is_write)>;

    void set_watchpoint_entries(std::vector<WatchpointEntry> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const WatchpointEntry& a, const WatchpointEntry& b) {
                      return a.start < b.start;
                  });
        watchpoint_entries_ = std::move(entries);
    }

    void set_watchpoint_hit_callback(WatchpointHitCallback cb) {
        on_watchpoint_hit_ = std::move(cb);
    }

    const std::vector<WatchpointEntry>& watchpoint_entries() const { return watchpoint_entries_; }

    // Direct watchpoint entry management (for C++ tests)
    void add_watchpoint_entry(WatchpointEntry entry) {
        watchpoint_entries_.push_back(std::move(entry));
        std::sort(watchpoint_entries_.begin(), watchpoint_entries_.end(),
                  [](const WatchpointEntry& a, const WatchpointEntry& b) {
                      return a.start < b.start;
                  });
    }

    void clear_watchpoint_entries() { watchpoint_entries_.clear(); }

    TubeShared* tube_shared() const { return shared_; }

    // --- Component access ---

    M6502& cpu() { return cpu_.cpu(); }
    const M6502& cpu() const { return cpu_.cpu(); }

    ParasiteCpu& parasite_cpu() { return cpu_; }
    const ParasiteCpu& parasite_cpu() const { return cpu_; }

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
    std::atomic<bool> in_run_{false};

    // Sequence counter
    std::atomic<uint64_t> sequence_{0};

    // Breakpoint addresses (sorted, checked inline at instruction boundaries)
    std::vector<BreakpointEntry> breakpoint_entries_;
    BreakpointHitCallback on_breakpoint_hit_;

    // Watchpoint entries (sorted by start, checked inline on every bus access)
    std::vector<WatchpointEntry> watchpoint_entries_;
    WatchpointHitCallback on_watchpoint_hit_;
};

}  // namespace beebium
