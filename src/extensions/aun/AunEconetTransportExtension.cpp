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

#include "AunEconetTransportExtension.hpp"

#include "beebium/econet/AunPacket.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <charconv>
#include <iostream>
#include <sstream>
#include <string_view>

namespace beebium {

namespace {

// Split s on a single delimiter character. Empty trailing fields are
// preserved so callers can reliably tell a missing field from a malformed
// one.
std::vector<std::string> split_on(std::string_view s, char delim) {
    std::vector<std::string> result;
    std::string current;
    for (char c : s) {
        if (c == delim) {
            result.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    result.push_back(std::move(current));
    return result;
}

bool parse_uint(std::string_view s, unsigned long& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

}  // namespace

std::optional<std::uint16_t> AunEconetTransportExtension::parse_port(
        const std::string& value) {
    if (value.empty()) {
        return AUN_DEFAULT_PORT;
    }
    if (value == "none") {
        return std::nullopt;
    }
    unsigned long parsed = 0;
    if (!parse_uint(value, parsed) || parsed > 0xFFFF) {
        std::cerr << "AUN extension: invalid port '" << value
                  << "' -- using default " << AUN_DEFAULT_PORT << "\n";
        return AUN_DEFAULT_PORT;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::vector<AunEconetTransportExtension::PeerSpec>
AunEconetTransportExtension::parse_map(const std::string& value) {
    std::vector<PeerSpec> peers;
    if (value.empty()) return peers;

    // Top-level: ',' separated entries. Inside an entry: ';' separated
    // net, ip, port fields.
    for (const auto& entry : split_on(value, ',')) {
        if (entry.empty()) continue;
        auto fields = split_on(entry, ';');
        if (fields.size() != 3) {
            std::cerr << "AUN extension: malformed map entry '" << entry
                      << "' (expected net;ip;port) -- skipping\n";
            continue;
        }

        // net may be either a bare station (e.g. "254", net implicit 0)
        // or "net.stn" (e.g. "0.254"). Match the legacy --aun-map format.
        std::uint8_t net = 0;
        std::uint8_t stn = 0;
        auto net_dot_stn = split_on(fields[0], '.');
        if (net_dot_stn.size() == 2) {
            unsigned long n = 0, s = 0;
            if (!parse_uint(net_dot_stn[0], n) || !parse_uint(net_dot_stn[1], s)
                || n > 0xFF || s > 0xFF) {
                std::cerr << "AUN extension: invalid net.stn '" << fields[0]
                          << "' in map entry -- skipping\n";
                continue;
            }
            net = static_cast<std::uint8_t>(n);
            stn = static_cast<std::uint8_t>(s);
        } else if (net_dot_stn.size() == 1) {
            unsigned long s = 0;
            if (!parse_uint(net_dot_stn[0], s) || s > 0xFF) {
                std::cerr << "AUN extension: invalid station '" << fields[0]
                          << "' in map entry -- skipping\n";
                continue;
            }
            stn = static_cast<std::uint8_t>(s);
        } else {
            std::cerr << "AUN extension: malformed net.stn '" << fields[0]
                      << "' in map entry -- skipping\n";
            continue;
        }

        // IP address: dotted-quad, parse via inet_pton.
        in_addr addr{};
        if (inet_pton(AF_INET, fields[1].c_str(), &addr) != 1) {
            std::cerr << "AUN extension: invalid IP address '" << fields[1]
                      << "' in map entry -- skipping\n";
            continue;
        }

        // Port: decimal uint16.
        unsigned long port = 0;
        if (!parse_uint(fields[2], port) || port == 0 || port > 0xFFFF) {
            std::cerr << "AUN extension: invalid port '" << fields[2]
                      << "' in map entry -- skipping\n";
            continue;
        }

        peers.push_back(PeerSpec{
            net, stn, addr.s_addr, static_cast<std::uint16_t>(port)});
    }

    return peers;
}

std::unique_ptr<NetworkBackend>
AunEconetTransportExtension::create_backend(std::uint8_t station) {
    auto port_value = config_value("port");
    auto port = parse_port(port_value ? std::string(*port_value) : std::string{});
    if (!port.has_value()) {
        // port=none -- explicitly disabled.
        return nullptr;
    }

    // local_net is hardcoded to 0 to match AunBackend's existing
    // assumption that source frames carry net 0. (See the legacy
    // --aun-map net 0 requirement.)
    auto backend = std::make_unique<AunBackend>(0, station, *port);
    if (!backend->is_connected()) {
        std::cerr << "AUN extension: failed to bind UDP socket on port "
                  << *port << " -- network disabled\n";
        return nullptr;
    }

    auto map_value = config_value("map");
    auto peers = parse_map(map_value ? std::string(*map_value) : std::string{});
    for (const auto& p : peers) {
        backend->add_peer(p.net, p.stn, p.ip_addr_net_byte_order, p.port);
    }

    return backend;
}

}  // namespace beebium
