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

#include "HostSerialExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/HostSerialPort.hpp>
#ifndef _WIN32
#include <beebium/serial/PosixSerialPort.hpp>
#include <beebium/serial/PtyMaster.hpp>
#else
#include <beebium/serial/Win32SerialPort.hpp>
#endif

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace beebium {

std::span<const std::string_view> HostSerialExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> HostSerialExtension::provides() const {
    return {};
}

void HostSerialExtension::init(ExtensionContext& ctx) {
    const std::string mode = std::string(config_value("mode").value_or("pty"));
    const std::string path = std::string(config_value("path").value_or(""));
    int baud = 19200;
    if (auto baud_val = config_value("baud"); baud_val && !baud_val->empty()) {
        const int parsed = std::atoi(std::string(*baud_val).c_str());
        if (parsed > 0) {
            baud = parsed;
        }
    }

    std::unique_ptr<serial::HostSerialPort> port;
    std::string resolved_path;
    if (mode == "pty") {
#ifndef _WIN32
        // path (if given) is a stable symlink to the kernel-assigned slave.
        auto pty = std::make_unique<serial::PtyMaster>(path);
        if (!pty->is_open()) {
            throw std::runtime_error("host-serial: failed to create pty: "
                                     + std::string(pty->open_error()));
        }
        std::cout << "host-serial: pty ready at " << pty->advertised_path() << "\n";
        resolved_path = pty->advertised_path();
        port = std::move(pty);
#else
        throw std::runtime_error(
            "host-serial: mode=pty is not supported on Windows; use mode=device");
#endif
    } else if (mode == "device") {
        if (path.empty()) {
            throw std::runtime_error(
                "host-serial: mode=device requires path=<serial device>");
        }
#ifndef _WIN32
        auto dev = std::make_unique<serial::PosixSerialPort>(path, baud);
#else
        auto dev = std::make_unique<serial::Win32SerialPort>(path, baud);
#endif
        if (!dev->is_open()) {
            throw std::runtime_error("host-serial: failed to open '" + path + "': "
                                     + std::string(dev->open_error()));
        }
        std::cout << "host-serial: opened " << dev->device_path() << "\n";
        resolved_path = dev->device_path();
        port = std::move(dev);
    } else {
        throw std::runtime_error("host-serial: unknown mode '" + mode
                                 + "' (expected 'pty' or 'device')");
    }

    std::size_t tx_buffer = serial::kDefaultTxBackPressure;
    if (auto value = config_value("tx_buffer"); value && !value->empty()) {
        const long parsed = std::atol(std::string(*value).c_str());
        if (parsed > 0) {
            tx_buffer = static_cast<std::size_t>(parsed);
        }
    }

    serial::HostSerialEndpoint::Options options;
    options.tx_back_pressure = tx_buffer;
    options.device_path = resolved_path;
    options.baud = baud;
    // The Extension UI lets the user re-point the bridge at runtime. A reopen
    // always opens a *device* at the chosen path/baud (the "which serial port
    // does the bridge use" picker); pty creation is a startup-only mode.
    options.factory = [](const std::string& reopen_path, int reopen_baud)
        -> std::unique_ptr<serial::HostSerialPort> {
#ifndef _WIN32
        return std::make_unique<serial::PosixSerialPort>(reopen_path, reopen_baud);
#else
        return std::make_unique<serial::Win32SerialPort>(reopen_path, reopen_baud);
#endif
    };
    // Async state changes (hot-unplug, completed reopen) re-render the panel.
    options.on_async_state_change = [this] { ui_.mark_dirty(); };

    endpoint_ = std::make_unique<serial::HostSerialEndpoint>(std::move(port),
                                                             std::move(options));
    ctx.get<SerialPort>().attach(*endpoint_);
}

void HostSerialExtension::shutdown() {
    // Drops the endpoint, which stops and joins the reader thread. The machine
    // is being torn down, so the ULA is not ticking through the handle.
    endpoint_.reset();
}

}  // namespace beebium
