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

#ifndef BEEBIUM_EXT_LOOPBACK_SERIAL_EXTENSION_HPP
#define BEEBIUM_EXT_LOOPBACK_SERIAL_EXTENSION_HPP

#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/serial/SerialDevice.hpp>

#include <cstdint>
#include <deque>
#include <span>
#include <string_view>

namespace beebium {

// Built-in PeripheralExtension: a serial loopback plug. The extension IS the
// serial device -- bytes the Beeb transmits echo straight back to its receiver,
// the software equivalent of a loopback connector and a zero-config "does my
// serial path work at all" diagnostic.
//
// Unlike rpc-serial it is NOT driven by a client (no RPC); it is a self-
// contained TX->RX echo. CLI: --loopback-serial (no parameters).
class LoopbackSerialExtension : public PeripheralExtension, public SerialPortDevice {
public:
    LoopbackSerialExtension() = default;
    ~LoopbackSerialExtension() override = default;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // No Peripherals-sidebar panel: a fixed self-echo plug has nothing to show
    // or configure. The sidebar still lists it by name via ListExtensions.

    // --- SerialPortDevice: echo transmitted bytes back to the receiver. Only
    // touched on the emulation thread (no client), so no lock is needed. ---
    bool has_data() override { return !queue_.empty(); }
    uint8_t next_byte() override {
        uint8_t value = queue_.front();
        queue_.pop_front();
        return value;
    }
    void add_byte(uint8_t value) override { queue_.push_back(value); }

private:
    std::deque<uint8_t> queue_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_LOOPBACK_SERIAL_EXTENSION_HPP
