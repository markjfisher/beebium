// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "BusStretching.hpp"
#include "ClockTypes.hpp"
#include <6502/6502.h>
#include <concepts>
#include <functional>

namespace beebium {

// Concept to detect if a memory policy has PC-aware read/write methods.
// These are required for BBC Model B+ VDU driver code shadow RAM routing,
// where the hardware routes memory access based on the program counter.
template<typename T>
concept HasPcAwareMemory = requires(T& m, uint16_t addr, uint16_t pc, uint8_t val) {
    { m.read_with_pc(addr, pc) } -> std::same_as<uint8_t>;
    { m.write_with_pc(addr, val, pc) } -> std::same_as<void>;
};

// Callback types for CpuBinding debugging hooks
using CpuWatchpointCallback = std::function<void(uint16_t addr, uint8_t value, bool is_write)>;
using CpuInstructionCallback = std::function<void(uint16_t pc)>;

// CpuBinding wraps M6502 + MemoryPolicy for clock subscription.
//
// The CPU executes at 2MHz on every cycle (both edges of the clock).
// Memory access is handled internally after each CPU cycle.
//
// For 1MHz bus accesses (VIAs, CRTC, etc.), the binding implements
// bus stretching by pre-synchronizing the VIAs before the memory
// operation completes. This ensures the CPU sees the correctly
// synchronized device state.
//
// Optional debugging callbacks:
// - instruction_callback_: Called at start of each instruction
// - watchpoint_callback_: Called after each memory access
//
template<typename MemoryPolicy>
class CpuBinding {
public:
    M6502& cpu;
    MemoryPolicy& memory;

    // CPU executes every 2MHz cycle - needs both edges
    static constexpr ClockEdge clock_edges = ClockEdge::Both;
    static constexpr ClockRate clock_rate = ClockRate::Rate_2MHz;

    CpuBinding(M6502& cpu, MemoryPolicy& memory)
        : cpu(cpu), memory(memory), current_cycle_(0) {}

    // Execute CPU cycle - called on both rising and falling edges
    void tick_rising() {
        execute_cycle();
    }

    void tick_falling() {
        execute_cycle();
    }

    // Runtime configuration
    void set_watchpoint_callback(CpuWatchpointCallback cb) {
        watchpoint_callback_ = std::move(cb);
    }

    void set_instruction_callback(CpuInstructionCallback cb) {
        instruction_callback_ = std::move(cb);
    }

    void clear_watchpoint_callback() { watchpoint_callback_ = nullptr; }
    void clear_instruction_callback() { instruction_callback_ = nullptr; }

    const CpuInstructionCallback& instruction_callback() const { return instruction_callback_; }

    // 1MHz bus stretch detection
    // After each cycle, check if the memory access was to a 1MHz peripheral.
    // If so, the Machine should insert additional cycles for bus synchronization.
    bool needs_stretch() const { return needs_stretch_; }

    // Get number of stretch cycles used (for Machine to account for extra VIA ticks)
    uint8_t stretch_cycle_count() const { return stretch_count_; }

    // Returns true if VIAs were pre-ticked this cycle (don't tick again in system_clock)
    bool vias_pre_ticked() const { return vias_pre_ticked_; }

    // Reset the stretch flag after Machine has handled it
    void clear_stretch() { needs_stretch_ = false; stretch_count_ = 0; vias_pre_ticked_ = false; }

    // Set current cycle count (called by Machine before each tick)
    void set_current_cycle(uint64_t cycle) { current_cycle_ = cycle; }

private:
    CpuWatchpointCallback watchpoint_callback_;
    CpuInstructionCallback instruction_callback_;
    bool needs_stretch_ = false;
    bool vias_pre_ticked_ = false;
    uint8_t stretch_count_ = 0;
    uint64_t current_cycle_ = 0;

    void execute_cycle() {
        // Reset stretch flag at start of each cycle
        needs_stretch_ = false;
        vias_pre_ticked_ = false;
        stretch_count_ = 0;

        // Instruction callback at start of new instruction
        if (instruction_callback_ && M6502_IsAboutToExecute(&cpu)) {
            instruction_callback_(cpu.pc.w);
        }

        // Execute one CPU cycle (determines address and r/w direction)
        (*cpu.tfn)(&cpu);

        // Get the address for this memory access
        const uint16_t addr = cpu.abus.w;

        // For 1MHz accesses, pre-synchronize VIAs BEFORE the memory operation.
        // This implements Option B: split the cycle into sub-phases where
        // synchronization happens before data is read/written.
        //
        // On real hardware:
        //   1. CPU puts address on bus
        //   2. Bus controller detects 1MHz device
        //   3. Waits for 1MHz clock alignment (VIAs continue ticking)
        //   4. Memory access completes
        //
        // We model this by ticking VIAs for the stretch amount BEFORE
        // completing the memory read/write.
        if (is_1mhz_access(addr)) {
            stretch_count_ = stretch_cycles(current_cycle_);
            needs_stretch_ = true;
            vias_pre_ticked_ = true;

            // Determine which VIA (if any) is being accessed
            const bool is_system_via = (addr >= 0xFE40 && addr <= 0xFE5F);
            const bool is_user_via = (addr >= 0xFE60 && addr <= 0xFE7F);

            // Pre-tick only the VIA being accessed for synchronization.
            // Other 1MHz devices (CRTC, ACIA) don't need VIA pre-ticking.
            if (is_system_via || is_user_via) {
                for (uint8_t i = 0; i < stretch_count_; ++i) {
                    bool is_rising = ((current_cycle_ + 1 + i) & 1) != 0;
                    if (is_system_via) {
                        if (is_rising) memory.system_via.tick_rising();
                        else memory.system_via.tick_falling();
                    }
                    if (is_user_via) {
                        if (is_rising) memory.user_via.tick_rising();
                        else memory.user_via.tick_falling();
                    }
                }

                // For timer reads, skip prediction since we pre-ticked
                if (cpu.read) {
                    if (is_system_via) memory.system_via.skip_next_timer_prediction();
                    if (is_user_via) memory.user_via.skip_next_timer_prediction();
                }
            }
        }

        // Memory access - use PC-aware methods if available (for B+ shadow RAM routing)
        uint8_t value;
        if constexpr (HasPcAwareMemory<MemoryPolicy>) {
            // B+ and other machines with PC-dependent memory routing
            const uint16_t pc = cpu.pc.w;
            if (cpu.read) {
                value = memory.read_with_pc(addr, pc);
                cpu.dbus = value;
            } else {
                value = cpu.dbus;
                memory.write_with_pc(addr, value, pc);
            }
        } else {
            // Model B and other machines with simple memory mapping
            if (cpu.read) {
                value = memory.read(addr);
                cpu.dbus = value;
            } else {
                value = cpu.dbus;
                memory.write(addr, value);
            }
        }

        // Watchpoint callback (if set, caller handles address filtering)
        if (watchpoint_callback_) {
            watchpoint_callback_(addr, value, !cpu.read);
        }
    }
};

} // namespace beebium
