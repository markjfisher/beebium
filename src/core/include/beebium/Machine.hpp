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
#include "tube/TubeShared.hpp"

#include <6502/6502.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace beebium {

// IRQ device mask for M6502_SetDeviceIRQ
// The 6502 library supports multiple IRQ sources; each bit represents one source.
// Bit 0: System VIA IRQ, Bit 1: User VIA IRQ, Bit 2: Tube HIRQ
constexpr uint8_t kIrqDeviceMask = 0x07;

// NMI device mask for M6502_SetDeviceNMI
// The 6502 library supports multiple NMI sources; we use separate bits for each source.
// Bit 0: Disc controller NMI (WD1770 INTRQ on Model B+, 8271 on Model B)
// Bit 1: Econet ADLC NMI (MC6854 IRQ gated through INTON/INTOFF flip-flop)
constexpr uint8_t kDiscNmiDeviceMask = 0x01;
constexpr uint8_t kEconetNmiDeviceMask = 0x02;

// Breakpoint hit callback: called on the rare path when a breakpoint address matches.
// Receives the entry (for condition evaluation) and the PC.
using BreakpointHitCallback = std::function<void(const BreakpointEntry& bp, uint16_t pc)>;

// Watchpoint hit callback: same type as CpuBinding's inline check callback
using WatchpointHitCallback = CpuWatchpointHitCallback;

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
        in_nmi_handler_ = false;
        in_irq_handler_ = false;
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
        in_nmi_handler_ = false;
        in_irq_handler_ = false;
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

        // Tick the type-ahead queue to process queued keystrokes
        state_.memory.system_via_peripheral.tick_type_ahead();

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
        M6502_SetDeviceIRQ(&state_.cpu, kIrqDeviceMask, irq_mask ? 1 : 0);

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

        // Interrupt handler tracking: detect entry via M6502ReadType_Interrupt
        // and exit via RTI (opcode $40).  NMI has priority: if nmi_flags is
        // set at interrupt entry, it's an NMI; otherwise it's IRQ/BRK.
        if (state_.cpu.read == M6502ReadType_Interrupt) {
            if (state_.cpu.nmi_flags != 0) {
                in_nmi_handler_ = true;
            } else {
                in_irq_handler_ = true;
            }
        }
        if (M6502_IsAboutToExecute(&state_.cpu) && state_.cpu.dbus == 0x40) {
            // RTI: NMI exit takes priority (NMI can nest inside IRQ).
            if (in_nmi_handler_) {
                in_nmi_handler_ = false;
            } else if (in_irq_handler_) {
                in_irq_handler_ = false;
            }
        }

        ++state_.cycle_count;
        ++sequence_;
    }

    // Execute for the given number of cycles, or until paused (e.g., by breakpoint)
    void run(uint64_t cycles) {
        struct RunGuard {
            std::atomic<bool>& flag;
            RunGuard(std::atomic<bool>& f) : flag(f) { flag.store(true, std::memory_order_release); }
            ~RunGuard() { flag.store(false, std::memory_order_release); }
        } guard{in_run_};

        const uint64_t target = state_.cycle_count + cycles;
        while (state_.cycle_count < target && !paused_.load()) {
            // Check breakpoints before step(), when all register updates from
            // the previous instruction have been applied by the T0 tfn that set
            // read=Opcode.  Use opcode_pc (the address of the opcode about to
            // be decoded), not pc (which has already been advanced past it by
            // M6502_NextInstruction's post-increment).
            if (!breakpoint_entries_.empty() && M6502_IsAboutToExecute(&state_.cpu)) {
                uint16_t pc = state_.cpu.opcode_pc.w;
                for (auto& bp : breakpoint_entries_) {
                    if (bp.start > pc) break;  // sorted by start: early exit
                    if (bp.matches(pc)) {
                        if (on_breakpoint_hit_) on_breakpoint_hit_(bp, pc);
                        if (paused_.load()) return;
                    }
                }
            }

            step();

            // Check cross-processor debugger stop signal
            if (tube_shared_ &&
                tube_shared_->debugger_stop_signal.load(std::memory_order_relaxed)) {
                tube_shared_->debugger_stop_signal.store(false, std::memory_order_relaxed);
                pause();
                return;
            }
        }
    }

    // Execute one complete instruction (variable cycles)
    // Returns the number of cycles taken
    uint64_t step_instruction() {
        in_run_.store(true, std::memory_order_release);
        const uint64_t start = state_.cycle_count;
        do {
            step();
        } while (!M6502_IsAboutToExecute(&state_.cpu));
        in_run_.store(false, std::memory_order_release);
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
        if (tube_shared_) {
            tube_shared_->bus_stretch_cancel.store(true, std::memory_order_release);
        }
        ++sequence_;
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lock(debug_mutex_);
            paused_.store(false);
        }
        if (tube_shared_) {
            tube_shared_->bus_stretch_cancel.store(false, std::memory_order_release);
            // Clear any stale cross-processor stop signal so we don't
            // immediately re-stop on the first cycle of the new run.
            tube_shared_->debugger_stop_signal.store(false, std::memory_order_release);
        }
        debug_cv_.notify_all();
        ++sequence_;
    }

    // Call before stepping cycles to ensure bus stretch cancel is cleared.
    void prepare_for_step() {
        if (tube_shared_) {
            tube_shared_->bus_stretch_cancel.store(false, std::memory_order_release);
        }
    }

    // Set optional TubeShared pointer for cross-processor debugger integration.
    // Must be called after Tube installation but before the emulation loop starts.
    void set_tube_shared(TubeShared* shared) { tube_shared_ = shared; }
    TubeShared* tube_shared() const { return tube_shared_; }

    // Wait until the emulation loop has exited run() after a pause.
    // Call from an RPC thread after pause() to ensure exclusive access.
    void wait_until_idle() {
        while (in_run_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    // Block until not paused - call from emulation loop.
    // Returns immediately if shutdown is requested, allowing clean exit.
    void wait_if_paused() {
        std::unique_lock<std::mutex> lock(debug_mutex_);
        while (paused_.load() && !shutdown_requested_.load()) {
            debug_cv_.wait_for(lock, std::chrono::milliseconds(100));
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
    uint16_t pc() const { return state_.cpu.opcode_pc.w; }
    uint8_t p() const { return state_.cpu.p.value; }

    // Interrupt handler tracking
    bool in_nmi_handler() const { return in_nmi_handler_; }
    bool in_irq_handler() const { return in_irq_handler_; }

    // CPU register setters (for debugger) - each increments sequence_
    void set_a(uint8_t value) { state_.cpu.a = value; ++sequence_; }
    void set_x(uint8_t value) { state_.cpu.x = value; ++sequence_; }
    void set_y(uint8_t value) { state_.cpu.y = value; ++sequence_; }
    void set_sp(uint8_t value) { state_.cpu.s.b.l = value; ++sequence_; }
    void set_pc(uint16_t value) {
        state_.cpu.opcode_pc.w = value;
        state_.cpu.pc.w = value + 1;
        state_.cpu.dbus = state_.memory.peek(value);
        ++sequence_;
    }
    void set_p(uint8_t value) { state_.cpu.p.value = value; ++sequence_; }

    // Direct memory access (convenience)
    // Note: read() is non-const because some devices have read side effects (e.g., VIA interrupt flags)
    uint8_t read(uint16_t addr) { return state_.memory.read(addr); }
    void write(uint16_t addr, uint8_t value) { state_.memory.write(addr, value); ++sequence_; }

    // Side-effect-free read for debugger inspection
    uint8_t peek(uint16_t addr) const { return state_.memory.peek(addr); }

    // Breakpoint entry management (sorted by address, modified only while stopped)
    void set_breakpoint_entries(std::vector<BreakpointEntry> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const BreakpointEntry& a, const BreakpointEntry& b) {
                      return a.start < b.start;
                  });
        breakpoint_entries_ = std::move(entries);
    }

    void set_breakpoint_hit_callback(BreakpointHitCallback cb) {
        on_breakpoint_hit_ = std::move(cb);
    }

    const std::vector<BreakpointEntry>& breakpoint_entries() const { return breakpoint_entries_; }

    // Debugger watchpoint entry management (sorted by start, modified only while stopped)
    void set_watchpoint_entries(std::vector<WatchpointEntry> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const WatchpointEntry& a, const WatchpointEntry& b) {
                      return a.start < b.start;
                  });
        watchpoint_entries_ = std::move(entries);
        cpu_binding_.set_watchpoint_entries(&watchpoint_entries_);
    }

    void set_watchpoint_hit_callback(WatchpointHitCallback cb) {
        on_watchpoint_hit_ = std::move(cb);
        cpu_binding_.set_watchpoint_hit_callback(&on_watchpoint_hit_);
    }

    const std::vector<WatchpointEntry>& watchpoint_entries() const { return watchpoint_entries_; }

    // Direct watchpoint entry management (for C++ tests and in-process use).
    // Adds an entry and re-sorts the vector.
    void add_watchpoint_entry(WatchpointEntry entry) {
        watchpoint_entries_.push_back(std::move(entry));
        std::sort(watchpoint_entries_.begin(), watchpoint_entries_.end(),
                  [](const WatchpointEntry& a, const WatchpointEntry& b) {
                      return a.start < b.start;
                  });
        cpu_binding_.set_watchpoint_entries(&watchpoint_entries_);
    }

    void clear_watchpoint_entries() {
        watchpoint_entries_.clear();
        cpu_binding_.set_watchpoint_entries(&watchpoint_entries_);
    }

    // PC histogram for instruction execution profiling
    void set_pc_histogram(ProgramCounterHistogram* histogram) { pc_histogram_ = histogram; }
    ProgramCounterHistogram* pc_histogram() const { return pc_histogram_; }

    // Access to bindings for testing/debugging
    CpuBindingType& cpu_binding() { return cpu_binding_; }
    VideoBindingType& video_binding() { return video_binding_; }

private:
    State state_;
    CpuBindingType cpu_binding_;
    VideoBindingType video_binding_;
    SystemClockType system_clock_;

    std::vector<BreakpointEntry> breakpoint_entries_;    // sorted by address, modified only while stopped
    BreakpointHitCallback on_breakpoint_hit_;           // rare-path callback
    std::vector<WatchpointEntry> watchpoint_entries_;   // sorted by start, modified only while stopped
    WatchpointHitCallback on_watchpoint_hit_;           // rare-path callback
    ProgramCounterHistogram* pc_histogram_ = nullptr;

    // Debug pause/resume state (for debugger attach)
    mutable std::mutex debug_mutex_;
    std::condition_variable debug_cv_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> shutdown_requested_{false};  // For clean server shutdown
    std::atomic<bool> in_run_{false};              // True while run() is executing
    std::atomic<uint64_t> sequence_{0};  // Increments on any mutation
    TubeShared* tube_shared_ = nullptr;  // Optional, for cross-processor debugger integration

    // Break key state (true when Break is held, CPU halted)
    bool in_reset_ = false;

    // Interrupt handler tracking (for debugger)
    bool in_nmi_handler_ = false;
    bool in_irq_handler_ = false;

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
