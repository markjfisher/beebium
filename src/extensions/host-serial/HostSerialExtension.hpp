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

#ifndef BEEBIUM_EXT_HOST_SERIAL_EXTENSION_HPP
#define BEEBIUM_EXT_HOST_SERIAL_EXTENSION_HPP

#include "HostSerialEndpoint.hpp"
#include "HostSerialUi.hpp"

#include <beebium/extension/PeripheralExtension.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

#ifdef BEEBIUM_BUILD_SERVICE
class HostSerialServiceImpl;  // forward; defined when the service layer is built
#endif

// Built-in PeripheralExtension that bridges the BBC serial port (RS423) to a
// host PTY or an existing serial device. It owns a HostSerialEndpoint (a
// SerialPortDevice that runs a reader thread over the host port) and attaches
// it to the serial port via the SerialPort handle (ctx.get<SerialPort>()).
//
// This is "the real host-serial bridge" -- one of potentially several serial
// devices. An emulated device (e.g. a FujiNet) would be a sibling extension
// attaching to the same seam; the synthetic test peers are the loopback-serial
// and rpc-serial extensions.
//
// CLI: --host-serial mode=<pty|device>:path=<...>:baud=<n>
//   mode  : "pty" (default; create a pseudo-terminal) or "device" (open an
//           existing serial/pty device path)
//   path  : pty    -> optional stable symlink to the pty slave
//           device -> the device path to open (required)
//   baud  : device line speed (default 19200; ignored for pty)
class HostSerialExtension : public PeripheralExtension {
public:
    HostSerialExtension();
    // Defined in the .cpp where HostSerialServiceImpl is complete (the
    // unique_ptr<HostSerialServiceImpl> member needs it for destruction).
    ~HostSerialExtension() override;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // Typed config API (query + re-point), parallel to the GUI panel below.
    std::vector<grpc::Service*> grpc_services() override;

    // Peripherals-sidebar panel.
    ExtensionUi* ui() override { return &ui_; }

    // Non-owning accessor used by HostSerialUi (snapshot + request_reopen).
    // Null before init() / after shutdown().
    serial::HostSerialEndpoint* endpoint() { return endpoint_.get(); }

private:
    // ui_ is declared before endpoint_ so endpoint_ is destroyed FIRST: tearing
    // down its I/O threads (which may call ui_.mark_dirty via the async-state
    // callback) before ui_ dies, avoiding a use-after-free.
    HostSerialUi ui_{*this};
    std::unique_ptr<serial::HostSerialEndpoint> endpoint_;
#ifdef BEEBIUM_BUILD_SERVICE
    // Declared after endpoint_ so it is destroyed FIRST (it holds an endpoint_
    // reference). Lazily constructed in grpc_services().
    std::unique_ptr<HostSerialServiceImpl> service_;
#endif
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_HOST_SERIAL_EXTENSION_HPP
