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

#include "FourWayHandshake.hpp"
#include "Mc6854.hpp"
#include "NetworkBackend.hpp"

#include <cstdint>
#include <memory>

namespace beebium {

// Models the optional Econet hardware upgrade on the BBC Micro.
//
// On the Model B, this was a set of discrete chip positions: IC93 (MC68B54 ADLC),
// IC97 (NMI gating flip-flop), and supporting passives. On the Master series it
// was a pin header for a carrier board. This class abstracts both.
//
// The socket occupies two non-contiguous address ranges in SHEILA:
//   &FE18-&FE1F: Station ID register (read returns station number, triggers INTOFF)
//   &FEA0-&FEBF: ADLC registers (4 registers, mirrored)
//
// When empty (no Econet hardware fitted):
//   - Station ID region returns 0x00 (slow 1MHz open bus — pull-down resistors)
//   - ADLC region returns the last bus value (fast 2MHz open bus — capacitance)
//   - NMI is never asserted
//   - The DNFS/NFS ROM detects this and skips NFS initialisation
//
// When populated (--station N specified):
//   - Station ID register returns the configured station number
//   - ADLC register access delegates to the Mc6854
//   - NMI gating via INTON/INTOFF flip-flop is active
//   - ADLC is ticked on every 2MHz half-cycle
class EconetSocket {
public:
    EconetSocket() = default;

    // Non-copyable (owns the backend)
    EconetSocket(const EconetSocket&) = delete;
    EconetSocket& operator=(const EconetSocket&) = delete;

    // --- Configuration ---

    // Fit the Econet hardware: sets station number and connects the network backend.
    // When aun_mode is true, a FourWayHandshake decorator is inserted between the
    // ADLC and the backend to bridge AUN's two-way protocol to the four-way
    // handshake that NFS ROMs expect.
    void enable(uint8_t station_id, std::unique_ptr<NetworkBackend> backend,
                bool aun_mode = false) {
        backend_ = std::move(backend);
        if (aun_mode) {
            handshake_ = std::make_unique<FourWayHandshake>(*backend_);
            adlc_ = std::make_unique<Mc6854>(*handshake_);
        } else {
            handshake_.reset();
            adlc_ = std::make_unique<Mc6854>(*backend_);
        }
        station_id_ = station_id;
        enabled_ = true;
    }

    // Remove the Econet hardware (return to empty socket state).
    void disable() {
        adlc_.reset();
        handshake_.reset();
        backend_.reset();
        enabled_ = false;
        nmi_enable_ff_ = false;
    }

    bool enabled() const { return enabled_; }

    // Set pointer to the MemoryMap's last_bus_value for open bus emulation
    // in the ADLC region. Must be set before reads if accurate open bus is needed.
    void set_last_bus_value_ptr(const uint8_t* ptr) {
        last_bus_value_ptr_ = ptr;
    }

    // --- Station ID Region (&FE18-&FE1F) ---

    // Read from station ID register. Side-effect: triggers INTOFF (clears NMI enable).
    uint8_t read_station_id(uint16_t /*offset*/) {
        // INTOFF: reading the station ID register clears the NMI enable flip-flop.
        // This happens whether or not Econet hardware is fitted (the address decoding
        // drives the flip-flop reset pin). When empty, the flip-flop doesn't exist,
        // so the side-effect is harmless.
        nmi_enable_ff_ = false;

        if (enabled_) {
            return station_id_;
        }

        // Empty socket: slow 1MHz open bus returns 0x00 (pull-down resistors)
        return 0x00;
    }

    // Write to station ID register (no-op — read-only on real hardware).
    void write_station_id(uint16_t /*offset*/, uint8_t /*value*/) {
        // The station ID register is read-only. Writes are ignored.
        // INTOFF still fires on writes too (address decoding isn't R/W selective).
        nmi_enable_ff_ = false;
    }

    // --- ADLC Region (&FEA0-&FEBF) ---

    // Read from ADLC registers.
    uint8_t read_adlc(uint16_t offset) {
        if (enabled_ && adlc_) {
            return adlc_->read(offset);
        }

        // Empty socket: fast 2MHz open bus returns last bus value (capacitance)
        return last_bus_value_ptr_ ? *last_bus_value_ptr_ : 0x00;
    }

    // Write to ADLC registers.
    void write_adlc(uint16_t offset, uint8_t value) {
        if (enabled_ && adlc_) {
            adlc_->write(offset, value);
        }
        // Empty socket: writes fall through (ignored)
    }

    // --- INTON (called when &FE20 is accessed — Video ULA range) ---

    // Sets the NMI enable flip-flop. On real hardware, the Econet hardware
    // taps the same address decode select line as the Video ULA.
    void on_inton() {
        nmi_enable_ff_ = true;
    }

    // --- NMI output ---

    // Returns true when NMI should be asserted to the CPU.
    // Three conditions must all be true:
    //   1. Econet hardware is fitted (enabled_)
    //   2. NMI enable flip-flop is set (INTON fired, INTOFF not yet fired)
    //   3. ADLC IRQ output is active (interrupt condition exists)
    bool nmi_pending() const {
        return enabled_ && nmi_enable_ff_ && adlc_ && adlc_->irq_output();
    }

    // --- Clock ---

    // 2MHz clock edges — delegates to ADLC when populated, no-op when empty.
    // The handshake is ticked before the ADLC so that timeout-generated frames
    // are available when the ADLC's byte trickle calls receive_frame().
    void tick_rising() {
        if (enabled_) {
            if (handshake_) handshake_->tick();
            if (adlc_) adlc_->tick_rising();
        }
    }

    void tick_falling() {
        if (enabled_ && adlc_) {
            adlc_->tick_falling();
        }
    }

    // --- Reset ---

    void reset() {
        if (adlc_) {
            adlc_->hard_reset();
        }
        if (handshake_) {
            handshake_->reset();
        }
        nmi_enable_ff_ = false;
    }

    // --- Accessors for testing ---

    bool nmi_enable_ff() const { return nmi_enable_ff_; }
    uint8_t station_id() const { return station_id_; }
    Mc6854* adlc() { return adlc_.get(); }
    const Mc6854* adlc() const { return adlc_.get(); }
    FourWayHandshake* handshake() { return handshake_.get(); }
    const FourWayHandshake* handshake() const { return handshake_.get(); }

    NetworkBackend* backend() { return backend_.get(); }
    const NetworkBackend* backend() const { return backend_.get(); }
    bool aun_mode() const { return handshake_ != nullptr; }

private:
    std::unique_ptr<NetworkBackend> backend_;
    std::unique_ptr<FourWayHandshake> handshake_;
    std::unique_ptr<Mc6854> adlc_;
    uint8_t station_id_ = 0;
    bool enabled_ = false;
    bool nmi_enable_ff_ = false;
    const uint8_t* last_bus_value_ptr_ = nullptr;
};

}  // namespace beebium
