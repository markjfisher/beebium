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

#ifndef BEEBIUM_EXT_RPC_SERIAL_EXTENSION_HPP
#define BEEBIUM_EXT_RPC_SERIAL_EXTENSION_HPP

#include "RpcSerialEndpoint.hpp"

#include <beebium/extension/PeripheralExtension.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

#ifdef BEEBIUM_BUILD_SERVICE
class RpcSerialDispatcher;  // forward; defined when the service layer is built
#endif

// Built-in PeripheralExtension: a client-driven serial peer. The RPC client is
// the device on the far end of the BBC serial wire -- it injects bytes for the
// BBC to receive and collects bytes the BBC transmits, via the RpcSerial
// service tunnelled over the core's ExtensionRpc channel. The extension owns
// an RpcSerialEndpoint and attaches it to the serial port via the SerialPort
// handle. It does NOT link gRPC: the dispatcher takes serialized bytes.
//
// CLI: --rpc-serial (no parameters).
class RpcSerialExtension : public PeripheralExtension {
public:
    RpcSerialExtension();
    ~RpcSerialExtension() override;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // No Peripherals-sidebar panel: a client-driven peer has nothing to show or
    // configure here. Clients drive it through the unary RpcSerial methods
    // (there is no persistent session to report), and the byte queues are
    // debugger detail, exposed via RpcSerial.GetStatus rather than the sidebar.
    // The sidebar still lists the extension by name via ListExtensions.

private:
    std::unique_ptr<RpcSerialEndpoint> endpoint_;  // constructed in init() from config
#ifdef BEEBIUM_BUILD_SERVICE
    std::unique_ptr<RpcSerialDispatcher> dispatcher_;  // lazily constructed
#endif
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_RPC_SERIAL_EXTENSION_HPP
