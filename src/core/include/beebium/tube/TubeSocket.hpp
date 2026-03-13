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

#include "TubeUla.hpp"

#include <cstdint>

namespace beebium {

// Models the Tube connector on the underside of the BBC Micro motherboard.
//
// All BBC Micros have the Tube connector (active low active-select at &FEE0-&FEE7),
// but the Tube ULA and second processor are optional add-ons. This class follows
// the same empty/populated socket pattern as DiscControllerSocket and EconetSocket.
//
// When empty (no second processor attached):
//   - Reads return last bus value (2MHz open bus -- capacitance)
//   - Writes are ignored
//   - irq_pending() returns false (no HIRQ source)
//   - MOS OSBYTE &EA correctly detects no Tube present
//
// When enabled (second processor attached):
//   - Reads/writes delegate to the TubeUla host-side interface
//   - irq_pending() reflects HIRQ (Q=1 AND R4 P-to-H data available)
//
// The register offsets use 3 address bits (A0-A2), mirrored across &FEE0-&FEFF.
// The hardware policy registers this with Mirror<0x07>.
class TubeSocket {
public:
    TubeSocket() = default;

    // --- Configuration ---

    // Enable the Tube socket (attach a second processor).
    // Phase 1: uses the in-process TubeUla owned by this socket.
    // Phase 2 will accept a TubeShared* pointer to shared memory.
    void enable() {
        enabled_ = true;
    }

    // Disable the Tube socket (detach second processor).
    // Reverts to empty-socket behaviour.
    void disable() {
        enabled_ = false;
    }

    bool enabled() const { return enabled_; }

    // Set pointer to the MemoryMap's last_bus_value for open bus emulation.
    // The Tube address range (&FEE0-&FEFF) is on the 2MHz bus, so when empty
    // the data bus retains its previous value (capacitance).
    void set_last_bus_value_ptr(const uint8_t* ptr) {
        last_bus_value_ptr_ = ptr;
    }

    // --- MemoryMappedDevice interface ---

    uint8_t read(uint16_t offset) {
        if (!enabled_)
            return last_bus_value_ptr_ ? *last_bus_value_ptr_ : 0xFF;
        return tube_ula_.host_read(static_cast<uint8_t>(offset));
    }

    void write(uint16_t offset, uint8_t value) {
        if (!enabled_)
            return;
        tube_ula_.host_write(static_cast<uint8_t>(offset), value);
    }

    // --- IrqSource interface (satisfies IrqSource concept) ---
    //
    // Named irq_pending() to satisfy the generic IrqSource concept used by
    // IrqAggregator. This adapts the Tube-specific HIRQ signal (TubeUla::hirq())
    // to the machine-level IRQ aggregation framework. The Tube ULA uses the
    // domain-specific name "hirq" (host IRQ) because it has three distinct
    // interrupt outputs (HIRQ, PIRQ, PNMI) from the Tube Application Note 004.

    bool irq_pending() const {
        if (!enabled_)
            return false;
        return tube_ula_.hirq();
    }

    // --- Reset ---

    void reset() {
        tube_ula_.reset();
    }

    // --- Accessors for testing and Phase 2 integration ---

    TubeUla& tube_ula() { return tube_ula_; }
    const TubeUla& tube_ula() const { return tube_ula_; }

private:
    TubeUla tube_ula_;
    bool enabled_ = false;
    const uint8_t* last_bus_value_ptr_ = nullptr;
};

}  // namespace beebium
