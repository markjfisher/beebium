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
    // PNMI level is passed to M6502_SetDeviceNMI, which handles edge
    // detection internally (6502 NMI is edge-triggered on falling edge).
    // We use pnmi_level() rather than pnmi() because the latter only
    // updates during parasite_read/write and would miss host-initiated
    // R3 changes between register accesses.
    M6502_SetDeviceIRQ(&cpu_, kPirqMask, tube_port_.pirq() ? 1 : 0);
    M6502_SetDeviceNMI(&cpu_, kPnmiMask, tube_port_.pnmi_level() ? 1 : 0);

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
