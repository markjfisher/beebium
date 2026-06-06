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

#ifndef BEEBIUM_EXT_IP232_SERIAL_EXTENSION_HPP
#define BEEBIUM_EXT_IP232_SERIAL_EXTENSION_HPP

#include <beebium/extension/PeripheralExtension.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace beebium::ip232 {
class Ip232SerialEndpoint;
}

namespace beebium {

class SerialPort;

// Built-in PeripheralExtension that connects the BBC serial port (RS423) to a
// tcpser-style IP232 server over TCP. It owns an Ip232SerialEndpoint (a
// SerialPortDevice) and attaches it to the serial port via the SerialPort
// handle. The network sibling of host-serial: the same endpoint machinery with
// a socket + IP232 codec instead of a tty.
//
// CLI: --ip232-serial host=<host>:port=<n>:mode=<ip232|raw>:handshake=<bool>
//   host      : IP232 server hostname/address (default localhost)
//   port      : IP232 server TCP port (default 25232)
//   mode      : "ip232" (0xFF-escaped, persistent) or "raw" (pure pipe,
//               connect on RTS) (default ip232)
//   handshake : convey RTS to the server via the 0xFF escape (ip232 mode)
//   tx_buffer : transmit buffer size; /CTS asserts at/above it
class Ip232SerialExtension : public PeripheralExtension {
public:
    Ip232SerialExtension();
    ~Ip232SerialExtension() override;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

private:
    std::unique_ptr<ip232::Ip232SerialEndpoint> endpoint_;
    SerialPort* serial_port_ = nullptr;  // detached on shutdown
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_IP232_SERIAL_EXTENSION_HPP
