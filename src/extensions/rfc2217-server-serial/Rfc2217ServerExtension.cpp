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

#include "Rfc2217ServerExtension.hpp"

#include "Rfc2217ServerEndpoint.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialBufferLimits.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace beebium {

Rfc2217ServerExtension::Rfc2217ServerExtension() = default;
Rfc2217ServerExtension::~Rfc2217ServerExtension() = default;

std::span<const std::string_view> Rfc2217ServerExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> Rfc2217ServerExtension::provides() const {
    return {};
}

void Rfc2217ServerExtension::init(ExtensionContext& ctx) {
    rfc2217::Rfc2217ServerEndpoint::Options options;

    const std::string bind = std::string(config_value("bind").value_or("127.0.0.1"));
    options.bind = bind.empty() ? "127.0.0.1" : bind;

    int port = 0;
    if (auto value = config_value("port"); value && !value->empty()) {
        const int parsed = std::atoi(std::string(*value).c_str());
        if (parsed > 0 && parsed <= 65535) {
            port = parsed;
        } else {
            throw std::runtime_error("rfc2217-server-serial: port must be 1..65535, got '"
                                     + std::string(*value) + "'");
        }
    }
    if (port == 0) {
        throw std::runtime_error("rfc2217-server-serial: port=<n> is required");
    }
    options.port = static_cast<std::uint16_t>(port);

    options.tx_back_pressure = serial::kDefaultTxBackPressure;
    if (auto value = config_value("tx_buffer"); value && !value->empty()) {
        const long parsed = std::atol(std::string(*value).c_str());
        if (parsed > 0) options.tx_back_pressure = static_cast<std::size_t>(parsed);
    }

    const std::string bind_for_log = options.bind;
    auto endpoint = std::make_unique<rfc2217::Rfc2217ServerEndpoint>(std::move(options));
    if (!endpoint->is_listening()) {
        throw std::runtime_error("rfc2217-server-serial: cannot listen on "
                                 + bind_for_log + ":" + std::to_string(port));
    }

    endpoint_ = std::move(endpoint);
    serial_port_ = &ctx.get<SerialPort>();
    serial_port_->attach(*endpoint_);
    std::cout << "rfc2217-server-serial: listening on " << bind_for_log << ":" << port
              << " (RFC 2217, unauthenticated)\n";
}

void Rfc2217ServerExtension::shutdown() {
    if (serial_port_) {
        serial_port_->detach();
        serial_port_ = nullptr;
    }
    endpoint_.reset();
}

}  // namespace beebium
