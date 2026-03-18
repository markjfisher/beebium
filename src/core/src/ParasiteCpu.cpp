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

#include <beebium/tube/ParasiteCpu.hpp>

namespace beebium {

ParasiteCpu::ParasiteCpu(ParasiteMemoryMap& memory, TubeParasitePort& tube_port)
    : memory_(memory)
    , tube_port_(tube_port)
    , cycle_count_(0)
{
    M6502_Init(&cpu_, &M6502_rockwell65c02_config);
}

ParasiteCpu::~ParasiteCpu() {
    M6502_Destroy(&cpu_);
}

void ParasiteCpu::reset() {
    M6502_Init(&cpu_, &M6502_rockwell65c02_config);
    M6502_Reset(&cpu_);
    memory_.reset();
    tube_port_.reset();
    cycle_count_ = 0;
    in_nmi_handler_ = false;
}

void ParasiteCpu::tick() {
    // Execute one CPU cycle (determines address and r/w direction)
    (*cpu_.tfn)(&cpu_);

    // Perform bus access through the memory map
    const uint16_t addr = cpu_.abus.w;
    if (cpu_.read) {
        cpu_.dbus = memory_.read(addr);
    } else {
        memory_.write(addr, cpu_.dbus);
    }

    // Route Tube interrupt lines to CPU.
    // PIRQ is level-sensitive (directly drives IRQ).
    M6502_SetDeviceIRQ(&cpu_, kPirqMask, tube_port_.pirq() ? 1 : 0);

    // Detect NMI handler entry: CPU is in the interrupt sequence and
    // nmi_flags is still set (cleared later at T4).  We check nmi_flags
    // directly rather than using M6502_IsProbablyIRQ because that macro
    // only tests irq_flags != 0, which is true whenever PIRQ is asserted
    // -- even when the CPU is actually taking the higher-priority NMI.
    if (cpu_.read == M6502ReadType_Interrupt && cpu_.nmi_flags != 0) {
        in_nmi_handler_ = true;
        // Clear device_nmi_flags so that reasserting PNMI after RTI
        // produces a clean 0-to-1 edge in M6502_SetDeviceNMI.
        M6502_SetDeviceNMI(&cpu_, kPnmiMask, 0);
    }

    // Detect NMI handler exit: about to execute RTI (opcode $40).
    if (in_nmi_handler_ && M6502_IsAboutToExecute(&cpu_) && cpu_.dbus == 0x40) {
        in_nmi_handler_ = false;
    }

    // PNMI: only forward the level to M6502 when not inside an NMI handler.
    // This prevents NMI nesting in the cross-process Tube model where the
    // host thread can write the next R3 byte before the parasite's NMI
    // handler completes.  When the handler returns (RTI), suppression ends
    // and the current PNMI level produces a clean edge if still asserted.
    if (!in_nmi_handler_) {
        M6502_SetDeviceNMI(&cpu_, kPnmiMask, tube_port_.pnmi_level() ? 1 : 0);
    }

    // Complete deferred Tube register side effects at the END of this
    // tick. This models the real Tube ULA clearing the ready flag at
    // the end of the bus cycle, so the next tick's status reads see
    // the correct ready=0 state.
    tube_port_.complete_cycle();

    ++cycle_count_;
}

uint64_t ParasiteCpu::step_instruction() {
    const uint64_t start = cycle_count_;
    do {
        tick();
    } while (!M6502_IsAboutToExecute(&cpu_));
    return cycle_count_ - start;
}

void ParasiteCpu::run(uint64_t cycles) {
    const uint64_t target = cycle_count_ + cycles;
    while (cycle_count_ < target) {
        tick();
    }
}

}  // namespace beebium
