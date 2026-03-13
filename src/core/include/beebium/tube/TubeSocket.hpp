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

#include "TubeHostBackend.hpp"
#include "TubeHostPort.hpp"
#include "TubeUla.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace beebium {

// Null-object backend for an empty Tube socket (no second processor attached).
//
// Reads return the last bus value (2MHz open bus capacitance). Writes are
// ignored. No interrupts. Reset is a no-op. This is the default state of
// the socket before a second processor is attached.
//
// The bus value pointer is stored indirectly (pointer-to-pointer) so that
// set_last_bus_value_ptr() on the socket takes effect without reconstructing
// this object.
class EmptyTubeBackend : public TubeHostBackend {
public:
    explicit EmptyTubeBackend(const uint8_t* const* bus_value_ptr)
        : bus_value_ptr_(bus_value_ptr) {}

    uint8_t host_read(uint8_t) override {
        return (bus_value_ptr_ && *bus_value_ptr_) ? **bus_value_ptr_ : 0xFF;
    }
    void host_write(uint8_t, uint8_t) override {}
    bool hirq() const override { return false; }
    void reset() override {}

private:
    const uint8_t* const* bus_value_ptr_;
};

// Models the Tube connector on the underside of the BBC Micro motherboard.
//
// All BBC Micros have the Tube connector (active low active-select at &FEE0-&FEE7),
// but the Tube ULA and second processor are optional add-ons. This class follows
// the same empty/populated socket pattern as DiscControllerSocket and EconetSocket.
//
// The socket delegates all register access, IRQ queries, and reset to a
// TubeHostBackend. Three implementations exist:
//
//   EmptyTubeBackend  -- no second processor (reads return bus value)
//   TubeUla           -- in-process model (Phase 1, for single-process testing)
//   TubeHostPort      -- shared memory adapter (Phase 2, parasite in another process)
//
// The register offsets use 3 address bits (A0-A2), mirrored across &FEE0-&FEFF.
// The hardware policy registers this with Mirror<0x07>.
class TubeSocket {
public:
    TubeSocket()
        : backend_(std::make_unique<EmptyTubeBackend>(&last_bus_value_ptr_))
    {}

    // --- Configuration ---

    // Enable in in-process mode: both host and parasite sides are modelled
    // by a TubeUla. Useful for single-process testing where parasite_write/
    // parasite_read are called directly on the TubeUla.
    void enable() {
        backend_ = std::make_unique<TubeUla>();
    }

    // Enable in shared memory mode: the host side is handled by a
    // TubeHostPort that communicates with a parasite process via atomics
    // in the TubeShared region. The caller is responsible for the lifetime
    // of the TubeShared memory.
    void enable(TubeShared* shared) {
        backend_ = std::make_unique<TubeHostPort>(shared);
    }

    // Disable the Tube socket (detach second processor).
    // Reverts to empty-socket behaviour.
    void disable() {
        backend_ = std::make_unique<EmptyTubeBackend>(&last_bus_value_ptr_);
    }

    bool enabled() const {
        return dynamic_cast<EmptyTubeBackend*>(backend_.get()) == nullptr;
    }

    // Set pointer to the MemoryMap's last_bus_value for open bus emulation.
    // The Tube address range (&FEE0-&FEFF) is on the 2MHz bus, so when empty
    // the data bus retains its previous value (capacitance).
    void set_last_bus_value_ptr(const uint8_t* ptr) {
        last_bus_value_ptr_ = ptr;
    }

    // --- MemoryMappedDevice interface ---

    uint8_t read(uint16_t offset) {
        return backend_->host_read(static_cast<uint8_t>(offset));
    }

    void write(uint16_t offset, uint8_t value) {
        backend_->host_write(static_cast<uint8_t>(offset), value);
    }

    // --- IrqSource interface (satisfies IrqSource concept) ---
    //
    // Named irq_pending() to satisfy the generic IrqSource concept used by
    // IrqAggregator. This adapts the Tube-specific HIRQ signal to the
    // machine-level IRQ aggregation framework.

    bool irq_pending() const {
        return backend_->hirq();
    }

    // --- Reset ---

    void reset() {
        backend_->reset();
    }

    // --- Accessors ---

    // Access the underlying TubeUla (only valid in in-process mode).
    // Returns nullptr if the socket is empty or in shared memory mode.
    TubeUla* tube_ula() {
        return dynamic_cast<TubeUla*>(backend_.get());
    }
    const TubeUla* tube_ula() const {
        return dynamic_cast<const TubeUla*>(backend_.get());
    }

    // Returns true if the socket is in shared memory mode.
    bool shared_mode() const {
        return dynamic_cast<TubeHostPort*>(backend_.get()) != nullptr;
    }

private:
    std::unique_ptr<TubeHostBackend> backend_;
    const uint8_t* last_bus_value_ptr_ = nullptr;
};

}  // namespace beebium
