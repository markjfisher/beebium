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

#include "Rfc2217ClientExtension.hpp"

#include "Rfc2217ClientEndpoint.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/net/EndpointUrl.hpp>
#include <beebium/serial/SerialBufferLimits.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace beebium {

Rfc2217ClientExtension::Rfc2217ClientExtension() = default;
Rfc2217ClientExtension::~Rfc2217ClientExtension() = default;

std::span<const std::string_view> Rfc2217ClientExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> Rfc2217ClientExtension::provides() const {
    return {};
}

void Rfc2217ClientExtension::init(ExtensionContext& ctx) {
    rfc2217::Rfc2217ClientEndpoint::Options options;

    // Endpoint as url=[scheme://]host:port, or separate host=/port= -- not both.
    auto url = config_value("url");
    auto host = config_value("host");
    auto port = config_value("port");
    const bool have_url = url && !url->empty();
    const bool have_host_port = (host && !host->empty()) || (port && !port->empty());

    if (have_url) {
        if (have_host_port) {
            throw std::runtime_error(
                "rfc2217-client-serial: url= cannot be combined with host=/port=");
        }
        std::string error;
        auto endpoint = net::parse_endpoint_url(*url, error);
        if (!endpoint) {
            throw std::runtime_error("rfc2217-client-serial: invalid url '"
                                     + std::string(*url) + "': " + error);
        }
        options.host = endpoint->host;
        options.port = endpoint->port;
    } else {
        options.host = (host && !host->empty()) ? std::string(*host) : "localhost";
        int port_num = 0;
        if (port && !port->empty()) {
            const int parsed = std::atoi(std::string(*port).c_str());
            if (parsed > 0 && parsed <= 65535) {
                port_num = parsed;
            } else {
                throw std::runtime_error("rfc2217-client-serial: port must be 1..65535, got '"
                                         + std::string(*port) + "'");
            }
        }
        if (port_num == 0) {
            throw std::runtime_error(
                "rfc2217-client-serial: a url= or host=/port= endpoint is required");
        }
        options.port = static_cast<std::uint16_t>(port_num);
    }

    options.baud = 19200;
    if (auto value = config_value("baud"); value && !value->empty()) {
        const long parsed = std::atol(std::string(*value).c_str());
        if (parsed > 0) options.baud = static_cast<std::uint32_t>(parsed);
    }

    options.tx_back_pressure = serial::kDefaultTxBackPressure;
    if (auto value = config_value("tx_buffer"); value && !value->empty()) {
        const long parsed = std::atol(std::string(*value).c_str());
        if (parsed > 0) options.tx_back_pressure = static_cast<std::size_t>(parsed);
    }

    const std::string host_for_log = options.host;
    const unsigned port_for_log = options.port;
    endpoint_ = std::make_unique<rfc2217::Rfc2217ClientEndpoint>(std::move(options));
    serial_port_ = &ctx.get<SerialPort>();
    serial_port_->attach(*endpoint_);
    std::cout << "rfc2217-client-serial: connecting to " << host_for_log << ":"
              << port_for_log << "\n";
}

void Rfc2217ClientExtension::shutdown() {
    if (serial_port_) {
        serial_port_->detach();
        serial_port_ = nullptr;
    }
    endpoint_.reset();
}

}  // namespace beebium
