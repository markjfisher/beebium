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

#include "InstructionTrace.hpp"
#include "ParasiteMemoryMap.hpp"
#include "TubeParasitePort.hpp"

#include <6502/6502.h>
#include <cstdint>

namespace beebium {

// CPU wrapper for the 6502 second processor parasite.
//
// Wires a Rockwell 65C02 to the ParasiteMemoryMap for bus access and
// routes interrupt lines from TubeParasitePort (PIRQ -> IRQ, PNMI -> NMI).
//
// The parasite CPU runs at 3 MHz with no bus stretching -- every tick
// is a single CPU cycle with a memory access.
//
// IRQ device mask bits:
//   bit 0: Tube PIRQ (the only IRQ source on the parasite)
//
// NMI device mask bits:
//   bit 0: Tube PNMI (the only NMI source on the parasite)

class ParasiteCpu {
public:
    ParasiteCpu(ParasiteMemoryMap& memory, TubeParasitePort& tube_port);
    ~ParasiteCpu();

    // Non-copyable (M6502 contains internal pointers)
    ParasiteCpu(const ParasiteCpu&) = delete;
    ParasiteCpu& operator=(const ParasiteCpu&) = delete;

    // Reset CPU, memory map, and Tube port. Clears cycle count.
    void reset();

    // Execute one CPU cycle (one bus access).
    void tick();

    // Execute cycles until the next instruction boundary.
    // Returns the number of cycles taken.
    uint64_t step_instruction();

    // Execute for the given number of cycles.
    void run(uint64_t cycles);

    // Cycle counter.
    uint64_t cycle_count() const { return cycle_count_; }

    // Interrupt handler tracking.
    bool in_nmi_handler() const { return in_nmi_handler_; }

    // CPU state access.
    M6502& cpu() { return cpu_; }
    const M6502& cpu() const { return cpu_; }

    // Instruction trace buffer.  Disabled by default; enable via
    // trace().set_enabled(true).  Records PC, registers, and opcode
    // at every instruction boundary with zero overhead when disabled.
    InstructionTrace& trace() { return trace_; }
    const InstructionTrace& trace() const { return trace_; }

private:
    static constexpr M6502_DeviceIRQFlags kPirqMask = 1;
    static constexpr M6502_DeviceNMIFlags kPnmiMask = 1;

    ParasiteMemoryMap& memory_;
    TubeParasitePort& tube_port_;
    M6502 cpu_;
    uint64_t cycle_count_ = 0;

    // NMI handler tracking: while inside an NMI handler, PNMI updates are
    // suppressed to prevent NMI nesting.  In the cross-process Tube model,
    // the host thread can write the next R3 byte before the parasite's NMI
    // handler completes, which would cause a new NMI edge during the
    // handler.  In real hardware, the host is limited to 2 MHz and disc
    // byte rate (~16-32 us), giving the parasite time to finish before new
    // data arrives.
    //
    // Entry: detected when M6502ReadType_Interrupt and it's an NMI (not IRQ).
    // Exit:  detected when an RTI instruction (opcode $40) is about to execute.
    bool in_nmi_handler_ = false;

    // Instruction trace (not allocated until capacity is set; default 4M entries).
    InstructionTrace trace_;
};

}  // namespace beebium
