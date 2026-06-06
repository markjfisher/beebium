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

#ifndef BEEBIUM_EXT_RFC2217_CLIENT_EXTENSION_HPP
#define BEEBIUM_EXT_RFC2217_CLIENT_EXTENSION_HPP

#include <beebium/extension/PeripheralExtension.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace beebium::rfc2217 {
class Rfc2217ClientEndpoint;
}

namespace beebium {

class SerialPort;

// Built-in-shaped PeripheralExtension (shipped as a plugin) that connects the BBC
// serial port (RS423) to a remote RFC 2217 access server. Beebium is the Telnet
// client; this is the network sibling of host-serial that can set the remote
// UART's real baud.
//
// CLI: --rfc2217-client-serial url="rfc2217://host:port" [:baud=N]
//   or --rfc2217-client-serial host=<host>:port=<n>[:baud=N]
class Rfc2217ClientExtension : public PeripheralExtension {
public:
    Rfc2217ClientExtension();
    ~Rfc2217ClientExtension() override;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

private:
    std::unique_ptr<rfc2217::Rfc2217ClientEndpoint> endpoint_;
    SerialPort* serial_port_ = nullptr;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_RFC2217_CLIENT_EXTENSION_HPP
