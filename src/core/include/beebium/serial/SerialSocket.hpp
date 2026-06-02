// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "SerialDevice.hpp"
#include "SerialUla.hpp"
#include "../devices/Mc6850.hpp"

#include <cstdint>
#include <memory>

namespace beebium {

// Models the BBC Micro's on-board serial hardware: the MC6850 ACIA at
// &FE08-&FE0F and the Serial ULA (SERPROC) at &FE10-&FE1F.
//
// Unlike the Econet socket (an optional upgrade), the serial hardware is always
// fitted on a real BBC, so this socket is always present and active. When no
// host transport is connected the receive line simply idles and transmitted
// bytes are discarded; the ACIA still responds normally to register access.
//
// The ACIA interrupt output is wired to the shared CPU IRQ line (it is NOT an
// NMI source like Econet), so this socket exposes irq_pending() for inclusion
// in the machine's IRQ aggregator.
//
// Clocking: tick_rising() and tick_falling() are each called once per 2MHz
// cycle by the Machine step loop (exactly one fires per cycle), so the ULA's
// bit clock advances once per CPU cycle.
class SerialSocket {
public:
    SerialSocket() {
        ula_.set_source(nullptr);
        ula_.set_sink(nullptr);
    }

    // Non-copyable (owns the ACIA/ULA which hold internal references).
    SerialSocket(const SerialSocket&) = delete;
    SerialSocket& operator=(const SerialSocket&) = delete;

    // --- Transport configuration ---

    // Attach the serial device (the bidirectional endpoint the ULA receives
    // from and transmits to). The socket takes shared ownership and wires it to
    // the ULA's receive and transmit seams. Pass nullptr to detach: the receive
    // line idles and transmitted bytes are discarded.
    void set_device(std::shared_ptr<SerialPortDevice> device) {
        device_ = std::move(device);
        ula_.set_source(device_.get());
        ula_.set_sink(device_.get());
    }

    // Open-bus support for reads of the write-only Serial ULA latch.
    void set_last_bus_value_ptr(const uint8_t* ptr) { last_bus_value_ptr_ = ptr; }

    // --- ACIA region (&FE08-&FE0F, mirror low bit) ---
    uint8_t read_acia(uint16_t offset) { return acia_.read(offset); }
    void write_acia(uint16_t offset, uint8_t value) { acia_.write(offset, value); }

    // --- Serial ULA region (&FE10-&FE1F) ---
    // The SERPROC is a write-only latch; reads return fast 2MHz open bus
    // (last value driven on the data bus).
    uint8_t read_ula(uint16_t /*offset*/) {
        return last_bus_value_ptr_ ? *last_bus_value_ptr_ : 0xFF;
    }
    void write_ula(uint16_t offset, uint8_t value) { ula_.write(offset, value); }

    // --- Clock (called from Machine::step on every 2MHz cycle) ---
    void tick_rising() { ula_.tick(); }
    void tick_falling() { ula_.tick(); }

    // --- IRQ output (feeds the shared CPU IRQ line via the aggregator) ---
    bool irq_pending() const { return acia_.irq_pending(); }

    // --- Reset ---
    void reset() {
        acia_.reset();
        ula_.reset();
    }

    // --- Accessors for tests / debugging ---
    Mc6850& acia() { return acia_; }
    const Mc6850& acia() const { return acia_; }
    SerialUla& ula() { return ula_; }
    const SerialUla& ula() const { return ula_; }

private:
    Mc6850 acia_;
    SerialUla ula_{acia_};

    std::shared_ptr<SerialPortDevice> device_;
    const uint8_t* last_bus_value_ptr_ = nullptr;
};

}  // namespace beebium
