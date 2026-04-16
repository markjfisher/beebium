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

#include "beebium/econet/NetworkBackend.hpp"
#include "beebium/econet/piconet/PiconetConfig.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"

#include <memory>
#include <optional>

namespace beebium {

// Network backend that bridges Beebium's emulated Econet to a real Econet
// network via a Piconet USB-CDC serial device. Runs in aun_mode -- the
// FourWayHandshake decorator must be active because Piconet's wire-level
// four-way handshake is opaque to the host (frames in/out are atomic).
//
// Phase 3 scope: TX path only. send_frame() formats Piconet commands and
// writes them to the SerialPort. receive_frame() returns nullopt; the
// reader thread, RX path, station-change propagation, and lifecycle are
// added in phase 4.
//
// Threading contract (per docs/discussion/piconet-feasibility.md): writes
// to SerialPort happen only on the emulation thread (send_frame and the
// future on_station_id_changed). The reader thread, when added in phase 4,
// only reads. No internal mutex.
class PiconetBackend : public NetworkBackend {
public:
    // Constructor is dependency-injected with a SerialPort, so tests can
    // pass in MockPiconetSerial / FakePiconetDevice / PtySerialPort while
    // production code passes in PosixSerialPort.
    PiconetBackend(piconet::PiconetConfig config,
                   std::unique_ptr<piconet::SerialPort> serial);

    ~PiconetBackend() override;

    void send_frame(const NetworkFrame& frame) override;

    // Phase 4 will replace this with a real implementation.
    std::optional<NetworkFrame> receive_frame() override {
        return std::nullopt;
    }

    bool is_connected() const override {
        return serial_ && serial_->is_open();
    }

    // Accessor for diagnostics / gRPC introspection.
    const piconet::PiconetConfig& config() const { return config_; }

private:
    piconet::PiconetConfig config_;
    std::unique_ptr<piconet::SerialPort> serial_;
};

}  // namespace beebium
