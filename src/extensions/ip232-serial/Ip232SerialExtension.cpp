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

#include "Ip232SerialExtension.hpp"

#include "Ip232SerialEndpoint.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialBufferLimits.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace beebium {

Ip232SerialExtension::Ip232SerialExtension() = default;
Ip232SerialExtension::~Ip232SerialExtension() = default;

std::span<const std::string_view> Ip232SerialExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> Ip232SerialExtension::provides() const {
    return {};
}

void Ip232SerialExtension::init(ExtensionContext& ctx) {
    ip232::Ip232SerialEndpoint::Options options;

    const std::string host = std::string(config_value("host").value_or("localhost"));
    options.host = host.empty() ? "localhost" : host;

    int port = 25232;
    if (auto value = config_value("port"); value && !value->empty()) {
        const int parsed = std::atoi(std::string(*value).c_str());
        if (parsed > 0 && parsed <= 65535) {
            port = parsed;
        } else {
            throw std::runtime_error(
                "ip232-serial: port must be 1..65535, got '" + std::string(*value) + "'");
        }
    }
    options.port = static_cast<std::uint16_t>(port);

    const std::string mode = std::string(config_value("mode").value_or("ip232"));
    if (mode == "ip232") {
        options.raw = false;
    } else if (mode == "raw") {
        options.raw = true;
    } else {
        throw std::runtime_error("ip232-serial: unknown mode '" + mode
                                 + "' (expected 'ip232' or 'raw')");
    }

    options.handshake = config_bool("handshake", true);

    options.tx_back_pressure = serial::kDefaultTxBackPressure;
    if (auto value = config_value("tx_buffer"); value && !value->empty()) {
        const long parsed = std::atol(std::string(*value).c_str());
        if (parsed > 0) {
            options.tx_back_pressure = static_cast<std::size_t>(parsed);
        }
    }

    const std::string host_for_log = options.host;
    endpoint_ = std::make_unique<ip232::Ip232SerialEndpoint>(std::move(options));
    serial_port_ = &ctx.get<SerialPort>();
    serial_port_->attach(*endpoint_);
    std::cout << "ip232-serial: " << mode << " bridge to " << host_for_log << ":"
              << port << " (connecting)\n";
}

void Ip232SerialExtension::shutdown() {
    // Detach first so the ULA stops referencing the endpoint, then drop it
    // (which stops and joins its I/O threads).
    if (serial_port_) {
        serial_port_->detach();
        serial_port_ = nullptr;
    }
    endpoint_.reset();
}

}  // namespace beebium
