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
    if (mode == "pty") {
#ifndef _WIN32
        // path (if given) is a stable symlink to the kernel-assigned slave.
        auto pty = std::make_unique<serial::PtyMaster>(path);
        if (!pty->is_open()) {
            throw std::runtime_error("host-serial: failed to create pty: "
                                     + std::string(pty->open_error()));
        }
        std::cout << "host-serial: pty ready at " << pty->advertised_path() << "\n";
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
        port = std::move(dev);
    } else {
        throw std::runtime_error("host-serial: unknown mode '" + mode
                                 + "' (expected 'pty' or 'device')");
    }

    endpoint_ = std::make_unique<serial::HostSerialEndpoint>(std::move(port));
    ctx.get<SerialPort>().attach(*endpoint_);
}

void HostSerialExtension::shutdown() {
    // Drops the endpoint, which stops and joins the reader thread. The machine
    // is being torn down, so the ULA is no longer ticking through the handle.
    endpoint_.reset();
}

}  // namespace beebium
