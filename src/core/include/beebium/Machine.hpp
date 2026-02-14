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

#ifndef BEEBIUM_MACHINE_HPP
#define BEEBIUM_MACHINE_HPP

#include "BusStretching.hpp"
#include "Clock.hpp"
#include "ClockBinding.hpp"
#include "CpuBinding.hpp"
#include "ProgramCounterHistogram.hpp"
#include "Types.hpp"
#include "Via6522.hpp"
#include "VideoBinding.hpp"
#include "econet/EconetConcepts.hpp"

#include <6502/6502.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <type_traits>
#include <vector>

namespace beebium {

// IRQ device mask for M6502_SetDeviceIRQ
// The 6502 library supports multiple IRQ sources; we use mask 0x03 for VIA IRQs
// Bit 0: System VIA IRQ, Bit 1: User VIA IRQ
constexpr uint8_t kViaIrqDeviceMask = 0x03;

// NMI device mask for M6502_SetDeviceNMI
// The 6502 library supports multiple NMI sources; we use separate bits for each source.
// Bit 0: Disc controller NMI (WD1770 INTRQ on Model B+, 8271 on Model B)
// Bit 1: Econet ADLC NMI (MC6854 IRQ gated through INTON/INTOFF flip-flop)
constexpr uint8_t kDiscNmiDeviceMask = 0x01;
constexpr uint8_t kEconetNmiDeviceMask = 0x02;

// Watchpoint callback: addr, value, is_write, cycle
using WatchCallback = std::function<void(uint16_t addr, uint8_t value, bool is_write, uint64_t cycle)>;

// PC callback: called before each instruction executes
using InstructionCallback = std::function<bool(uint16_t pc, uint64_t cycle)>;  // return false to stop

// Simple watchpoint structure
struct Watchpoint {
    uint32_t start_addr;
    uint32_t end_addr;  // exclusive, allows 0x10000 for full address space
    WatchType type;
    WatchCallback callback;

    Watchpoint(uint32_t start, uint32_t end, WatchType t, WatchCallback cb)
        : start_addr(start), end_addr(end), type(t), callback(std::move(cb))
    {
        assert(start_addr <= 0xFFFF && "start_addr must fit in 16-bit address space");
        assert(end_addr <= 0x10000 && "end_addr must be <= 0x10000");
        assert(start_addr < end_addr && "start_addr must be less than end_addr");
    }

    bool matches(uint16_t addr, bool is_write) const {
        if (addr < start_addr || addr >= end_addr) return false;
        if (is_write && (type & WATCH_WRITE)) return true;
        if (!is_write && (type & WATCH_READ)) return true;
        return false;
    }
};

// Machine state that can be serialized/deserialized.
// Parameterized by MemoryPolicy to include memory state.
template<typename MemoryPolicy>
struct MachineState {
    M6502 cpu{};
    MemoryPolicy memory;
    uint64_t cycle_count = 0;
};

// Core BBC Micro emulator, parameterized by CPU and Memory policies.
//
// CpuPolicy must provide:
//   - static constexpr const M6502Config* config
//
// MemoryPolicy must provide:
//   - uint8_t read(uint16_t addr) const
//   - void write(uint16_t addr, uint8_t value)
//   - void reset()
//   - system_via, user_via members (Via6522)
//   - irq_aggregator() method returning aggregator with poll()
//
template<typename CpuPolicy, typename MemoryPolicy>
class Machine {
public:
    // Policy type aliases for external access
    using Cpu = CpuPolicy;
    using Memory = MemoryPolicy;

    using State = MachineState<MemoryPolicy>;
    using CpuBindingType = CpuBinding<MemoryPolicy>;
    using VideoBindingType = VideoBinding<MemoryPolicy>;

    // System clock type: CPU, VIAs, and video all subscribe
    using SystemClockType = Clock<
        ClockBinding<CpuBindingType>,
        ClockBinding<Via6522>,
        ClockBinding<Via6522>,
        ClockBinding<VideoBindingType>
    >;

    Machine()
        : state_()
        , cpu_binding_(state_.cpu, state_.memory)
        , video_binding_(state_.memory)
        , system_clock_(make_system_clock())
    {
        setup_callbacks();
        reset();
    }

    // Note: VideoBinding is now explicitly constructed, taking Hardware by reference.
    // It internally owns a VideoRenderer for pixel generation.

    ~Machine() {
        M6502_Destroy(&state_.cpu);
    }

    // Non-copyable (contains M6502 with pointers)
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    // =========================================================================
    // Reset methods
    // =========================================================================

    // Power-on reset: clear RAM and reset all devices including System VIA.
    // This is the full initialization state, equivalent to powering off and on.
    // MOS will detect the cleared System VIA and perform full initialization.
    void reset() {
        M6502_Init(&state_.cpu, CpuPolicy::config);
        M6502_Reset(&state_.cpu);
        state_.memory.reset();
        video_binding_.reset();
        state_.cycle_count = 0;
        in_reset_ = false;
        ++sequence_;
    }

    // Soft reset (Break key): reset CPU and peripherals, but preserve System VIA.
    // The System VIA's preserved state allows MOS to detect this as a warm reset.
    // Does NOT clear RAM - programs and variables survive.
    // This is what happens when the Break key is released.
    void soft_reset() {
        M6502_Init(&state_.cpu, CpuPolicy::config);
        M6502_Reset(&state_.cpu);
        state_.memory.soft_reset();
        video_binding_.reset();
        // Do NOT reset cycle_count - maintains timing continuity
        in_reset_ = false;
        ++sequence_;
    }

    // =========================================================================
    // Break key handling (directly connected to reset circuit)
    // =========================================================================

    // Assert Break key (hold reset line low, halting CPU)
    // While Break is held, the CPU is frozen - step() will not execute instructions.
    void break_down() {
        in_reset_ = true;
        M6502_Halt(&state_.cpu);
        ++sequence_;
    }

    // Release Break key (begin reset sequence)
    // This always performs a soft reset - hardware does NOT distinguish Ctrl-Break.
    // The System VIA is preserved, and MOS checks the keyboard matrix during its
    // reset sequence. If Ctrl is held, MOS itself clears the VIA configuration
    // to force a "hard reset" behavior.
    void break_up() {
        soft_reset();
    }

    // Check if Break key is currently held (CPU halted)
    bool is_in_reset() const {
        return in_reset_;
    }

    // Execute one CPU cycle
    void step() {
        // Handle 1MHz bus stretch cycles.
        // During stretch, CPU is halted. VIAs have already been pre-ticked
        // by CpuBinding before the memory access, so we only tick video here.
        if (stretch_cycles_remaining_ > 0) {
            tick_video_only();
            --stretch_cycles_remaining_;
            ++state_.cycle_count;
            ++sequence_;
            return;
        }

        // Pass current cycle to CpuBinding for 1MHz synchronization calculations
        cpu_binding_.set_current_cycle(state_.cycle_count);

        // Tick CPU first - this may pre-tick VIAs for 1MHz synchronization
        const bool is_rising = (state_.cycle_count & 1) != 0;
        if (is_rising) {
            cpu_binding_.tick_rising();
        } else {
            cpu_binding_.tick_falling();
        }

        // Tick order matters for same-cycle vsync detection:
        // - On rising edge: VIA ticks (no video)
        // - On falling edge: Video ticks FIRST (updates vsync), then VIA (detects edge)
        // This matches jsbeeb where setVBlankInt() is called from video.polltime()
        // and immediately triggers VIA CA1 edge detection on the same cycle.
        if (is_rising) {
            // Rising edge: VIA only
            if (!cpu_binding_.vias_pre_ticked()) {
                state_.memory.system_via.tick_rising();
                state_.memory.user_via.tick_rising();
            }
        } else {
            // Falling edge: Video first (updates vsync), then VIA (detects edge)
            const auto video_rate = video_binding_.clock_rate();
            if (video_rate == ClockRate::Rate_2MHz || (state_.cycle_count & 1) == 0) {
                video_binding_.tick_falling();
            }
            if (!cpu_binding_.vias_pre_ticked()) {
                state_.memory.system_via.tick_falling();
                state_.memory.user_via.tick_falling();
            }
        }

        // Tick sound chip at 2 MHz if audio output is enabled
        if (state_.memory.audio_buffer) {
            state_.memory.sound_chip.tick(state_.memory.audio_buffer.value());
        }

        // Tick Econet ADLC at 2MHz (no-op when socket is empty).
        // The ADLC sits on the 2MHz bus at &FEA0-&FEBF and needs clocking
        // on every E-clock edge to drive the byte trickle timer.
        if constexpr (HasEconetSocket<MemoryPolicy>) {
            if (is_rising) {
                state_.memory.econet_socket.tick_rising();
            } else {
                state_.memory.econet_socket.tick_falling();
            }
        }

        // Check if the CPU's memory access triggered bus stretching
        if (cpu_binding_.needs_stretch()) {
            // VIAs were already pre-ticked by CpuBinding for synchronization.
            // We just need to account for the extra cycles where CPU is halted.
            stretch_cycles_remaining_ = cpu_binding_.stretch_cycle_count();
            cpu_binding_.clear_stretch();
        }

        // IRQ handling - poll aggregator and set CPU IRQ line
        uint8_t irq_mask = state_.memory.poll_irq();
        M6502_SetDeviceIRQ(&state_.cpu, kViaIrqDeviceMask, irq_mask ? 1 : 0);

        // NMI handling — disc controller at 1MHz, Econet at 2MHz.
        //
        // Disc NMI: only update on 1MHz clock edges (every other 2MHz cycle).
        // The WD1770 disc controller runs at 1MHz. Updating NMI every 2MHz cycle
        // causes DRQ to toggle too rapidly: after the NMI handler reads the data
        // register (clearing DRQ), the next tick() would immediately set DRQ for
        // the next byte, creating a new falling edge on /NMI before the handler
        // completes RTI. This causes NMIs to stack up infinitely.
        if ((state_.cycle_count & 1) == 0) {
            uint8_t nmi_mask = state_.memory.poll_nmi();
            M6502_SetDeviceNMI(&state_.cpu, kDiscNmiDeviceMask, nmi_mask ? 1 : 0);
        }

        // Econet NMI: update every 2MHz cycle (ADLC is a 2MHz device).
        // The ADLC IRQ output is gated through the INTON/INTOFF flip-flop
        // in EconetSocket::nmi_pending(). When all three conditions are met
        // (socket enabled, NMI flip-flop set, ADLC IRQ active), NMI is asserted.
        if constexpr (HasEconetSocket<MemoryPolicy>) {
            uint8_t econet_nmi = state_.memory.econet_socket.nmi_pending() ? 1 : 0;
            M6502_SetDeviceNMI(&state_.cpu, kEconetNmiDeviceMask, econet_nmi);
        }

        ++state_.cycle_count;
        ++sequence_;
    }

    // Execute for the given number of cycles, or until paused (e.g., by breakpoint)
    void run(uint64_t cycles) {
        const uint64_t target = state_.cycle_count + cycles;

        // Fast path when no debug callback is registered
        if (!on_instruction_) {
            while (state_.cycle_count < target && !paused_.load()) {
                step();
            }
            return;
        }

        // Debug path: check instruction callback at each instruction boundary
        while (state_.cycle_count < target && !paused_.load()) {
            // Call callback at instruction boundary (before fetch)
            if (M6502_IsAboutToExecute(&state_.cpu)) {
                if (!on_instruction_(state_.cpu.pc.w, state_.cycle_count)) {
                    return;  // Callback requested stop
                }
            }
            step();
        }
    }

    // Execute one complete instruction (variable cycles)
    // Returns the number of cycles taken
    uint64_t step_instruction() {
        const uint64_t start = state_.cycle_count;
        do {
            step();
        } while (!M6502_IsAboutToExecute(&state_.cpu));
        return state_.cycle_count - start;
    }

    // State access
    const State& state() const { return state_; }
    State& state() { return state_; }

    // CPU access
    const M6502& cpu() const { return state_.cpu; }
    M6502& cpu() { return state_.cpu; }

    // Memory access
    const MemoryPolicy& memory() const { return state_.memory; }
    MemoryPolicy& memory() { return state_.memory; }

    // Cycle counter
    uint64_t cycle_count() const { return state_.cycle_count; }

    // Sequence counter (increments on any mutation, for change detection)
    uint64_t sequence() const { return sequence_.load(); }

    // Debug pause/resume for debugger integration
    bool is_paused() const { return paused_.load(); }

    void pause() {
        paused_.store(true);
        ++sequence_;
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lock(debug_mutex_);
            paused_.store(false);
        }
        debug_cv_.notify_all();
        ++sequence_;
    }

    // Block until not paused - call from emulation loop
    // Returns immediately if shutdown is requested, allowing clean exit.
    // Uses a bounded wait (100ms) so the caller can poll for shutdown signals.
    void wait_if_paused() {
        if (paused_.load()) {
            std::unique_lock<std::mutex> lock(debug_mutex_);
            debug_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !paused_.load() || shutdown_requested_.load();
            });
        }
    }

    // Request clean shutdown - unblocks wait_if_paused()
    void request_shutdown() {
        shutdown_requested_.store(true);
        debug_cv_.notify_all();  // Wake up any blocked wait_if_paused()
    }

    // Check if shutdown has been requested
    bool shutdown_requested() const { return shutdown_requested_.load(); }

    // CPU register accessors (debugger convenience)
    uint8_t a() const { return state_.cpu.a; }
    uint8_t x() const { return state_.cpu.x; }
    uint8_t y() const { return state_.cpu.y; }
    uint8_t sp() const { return state_.cpu.s.b.l; }
    uint16_t pc() const { return state_.cpu.pc.w; }
    uint8_t p() const { return state_.cpu.p.value; }

    // CPU register setters (for debugger) - each increments sequence_
    void set_a(uint8_t value) { state_.cpu.a = value; ++sequence_; }
    void set_x(uint8_t value) { state_.cpu.x = value; ++sequence_; }
    void set_y(uint8_t value) { state_.cpu.y = value; ++sequence_; }
    void set_sp(uint8_t value) { state_.cpu.s.b.l = value; ++sequence_; }
    void set_pc(uint16_t value) { state_.cpu.pc.w = value; ++sequence_; }
    void set_p(uint8_t value) { state_.cpu.p.value = value; ++sequence_; }

    // Direct memory access (convenience)
    // Note: read() is non-const because some devices have read side effects (e.g., VIA interrupt flags)
    uint8_t read(uint16_t addr) { return state_.memory.read(addr); }
    void write(uint16_t addr, uint8_t value) { state_.memory.write(addr, value); ++sequence_; }

    // Side-effect-free read for debugger inspection
    uint8_t peek(uint16_t addr) const { return state_.memory.peek(addr); }

    // Watchpoint management
    void add_watchpoint(uint32_t addr, uint32_t length, WatchType type, WatchCallback callback) {
        watchpoints_.emplace_back(addr, addr + length, type, std::move(callback));
    }

    void clear_watchpoints() { watchpoints_.clear(); }

    const std::vector<Watchpoint>& watchpoints() const { return watchpoints_; }

    // Instruction callback
    void set_instruction_callback(InstructionCallback cb) { on_instruction_ = std::move(cb); }

    void clear_callbacks() {
        on_instruction_ = nullptr;
        watchpoints_.clear();
        pc_histogram_ = nullptr;
    }

    // PC histogram for instruction execution profiling
    void set_pc_histogram(ProgramCounterHistogram* histogram) { pc_histogram_ = histogram; }
    ProgramCounterHistogram* pc_histogram() const { return pc_histogram_; }

    // Execute one complete instruction with optional callback
    // Returns false if callback requested stop, true otherwise
    bool step_instruction_debug() {
        if (on_instruction_) {
            if (!on_instruction_(state_.cpu.pc.w, state_.cycle_count)) {
                return false;  // Callback requested stop
            }
        }
        step_instruction();
        return true;
    }

    // Access to bindings for testing/debugging
    CpuBindingType& cpu_binding() { return cpu_binding_; }
    VideoBindingType& video_binding() { return video_binding_; }

private:
    State state_;
    CpuBindingType cpu_binding_;
    VideoBindingType video_binding_;
    SystemClockType system_clock_;

    std::vector<Watchpoint> watchpoints_;
    InstructionCallback on_instruction_;
    ProgramCounterHistogram* pc_histogram_ = nullptr;

    // Debug pause/resume state (for debugger attach)
    mutable std::mutex debug_mutex_;
    std::condition_variable debug_cv_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> shutdown_requested_{false};  // For clean server shutdown
    std::atomic<uint64_t> sequence_{0};  // Increments on any mutation

    // Break key state (true when Break is held, CPU halted)
    bool in_reset_ = false;

    // 1MHz bus stretch handling
    // When CPU accesses a 1MHz peripheral, we insert extra cycles
    // where peripherals tick but the CPU doesn't
    uint8_t stretch_cycles_remaining_ = 0;

    SystemClockType make_system_clock() {
        return make_clock(
            make_clock_binding(cpu_binding_),
            make_clock_binding(state_.memory.system_via),
            make_clock_binding(state_.memory.user_via),
            make_clock_binding(video_binding_)
        );
    }

    // Tick video only during stretch cycles.
    // VIAs have already been pre-ticked by CpuBinding for 1MHz synchronization.
    // CPU is halted waiting for bus alignment.
    void tick_video_only() {
        const uint64_t cycle = state_.cycle_count;
        const bool is_rising = (cycle & 1) != 0;

        // Video ticks on falling edges only (ClockEdge::Falling)
        // Rate depends on current mode (1MHz for teletext, 2MHz for bitmap)
        if (!is_rising) {
            const auto video_rate = video_binding_.clock_rate();
            // 2MHz: tick every falling edge; 1MHz: tick on even falling edges only
            if (video_rate == ClockRate::Rate_2MHz || (cycle & 1) == 0) {
                video_binding_.tick_falling();
            }
        }
    }

    void setup_callbacks() {
        // Wire CpuBinding callbacks to Machine's debugging infrastructure

        // Watchpoint callback - dispatches to watchpoints vector
        cpu_binding_.set_watchpoint_callback(
            [this](uint16_t addr, uint8_t value, bool is_write) {
                if (!watchpoints_.empty()) {
                    for (const auto& wp : watchpoints_) {
                        if (wp.matches(addr, is_write)) {
                            wp.callback(addr, value, is_write, state_.cycle_count);
                        }
                    }
                }
            }
        );

        // Instruction callback - records PC histogram
        cpu_binding_.set_instruction_callback(
            [this](uint16_t pc) {
                if (pc_histogram_) {
                    pc_histogram_->record(pc);
                }
            }
        );
    }
};

} // namespace beebium

#endif // BEEBIUM_MACHINE_HPP
