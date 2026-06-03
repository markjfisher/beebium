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

#ifndef BEEBIUM_EXT_SERIAL_LOOPBACK_EXTENSION_HPP
#define BEEBIUM_EXT_SERIAL_LOOPBACK_EXTENSION_HPP

#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/serial/SerialDevice.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace beebium {

// Built-in PeripheralExtension: a serial loopback plug. Attaches a
// LoopbackSerialEndpoint to the BBC serial port so bytes the Beeb transmits
// echo straight back to its receiver -- the software equivalent of a loopback
// connector, and a zero-config "does my serial path work at all" diagnostic.
//
// Unlike rpc-serial it is NOT driven by a client (no RPC); it is a self-
// contained TX->RX echo. CLI: --serial-loopback (no parameters).
class SerialLoopbackExtension : public PeripheralExtension {
public:
    SerialLoopbackExtension() = default;
    ~SerialLoopbackExtension() override = default;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

private:
    std::unique_ptr<LoopbackSerialEndpoint> endpoint_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_SERIAL_LOOPBACK_EXTENSION_HPP
