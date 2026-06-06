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

#ifndef BEEBIUM_NET_ENDPOINT_URL_HPP
#define BEEBIUM_NET_ENDPOINT_URL_HPP

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace beebium::net {

// A network endpoint parsed from a `[scheme://]host:port` string -- the `url=`
// form shared by the network-serial extensions (ip232-serial now, the coming
// rfc2217 client/server). The scheme is optional and informational; the
// extension already knows its own protocol.
struct EndpointUrl {
    std::string scheme;  // empty if the input had no `scheme://` prefix
    std::string host;    // hostname / IPv4 / IPv6 literal (no brackets)
    std::uint16_t port = 0;
};

// Parse `[scheme://]host:port`. Returns the parsed parts, or nullopt with a
// human-readable reason in `error`. An IPv6 literal must be bracketed:
// `[::1]:port`. The port is required and must be 1..65535.
inline std::optional<EndpointUrl> parse_endpoint_url(std::string_view input,
                                                     std::string& error) {
    EndpointUrl result;

    if (input.empty()) {
        error = "empty endpoint (expected [scheme://]host:port)";
        return std::nullopt;
    }

    // Optional scheme:// prefix.
    if (auto sep = input.find("://"); sep != std::string_view::npos) {
        std::string_view scheme = input.substr(0, sep);
        if (scheme.empty()) {
            error = "missing scheme before '://'";
            return std::nullopt;
        }
        for (char c : scheme) {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
            if (!ok) {
                error = "invalid character in scheme '" + std::string(scheme) + "'";
                return std::nullopt;
            }
        }
        result.scheme.assign(scheme);
        input.remove_prefix(sep + 3);
    }

    // host:port -- split at the colon that separates them. An IPv6 literal is
    // bracketed so its internal colons are not mistaken for the separator.
    std::string_view host;
    std::string_view port_str;
    if (!input.empty() && input.front() == '[') {
        auto close = input.find(']');
        if (close == std::string_view::npos) {
            error = "unterminated '[' in IPv6 address";
            return std::nullopt;
        }
        host = input.substr(1, close - 1);
        std::string_view rest = input.substr(close + 1);
        if (rest.empty() || rest.front() != ':') {
            error = "missing ':port' after IPv6 address";
            return std::nullopt;
        }
        port_str = rest.substr(1);
    } else {
        auto colon = input.rfind(':');
        if (colon == std::string_view::npos) {
            error = "missing ':port' in '" + std::string(input) + "'";
            return std::nullopt;
        }
        host = input.substr(0, colon);
        port_str = input.substr(colon + 1);
    }

    if (host.empty()) {
        error = "missing host";
        return std::nullopt;
    }
    if (port_str.empty()) {
        error = "missing port";
        return std::nullopt;
    }

    unsigned long port = 0;
    auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
    if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
        error = "port '" + std::string(port_str) + "' is not a number";
        return std::nullopt;
    }
    if (port < 1 || port > 65535) {
        error = "port " + std::string(port_str) + " out of range (1..65535)";
        return std::nullopt;
    }

    result.host.assign(host);
    result.port = static_cast<std::uint16_t>(port);
    return result;
}

}  // namespace beebium::net

#endif  // BEEBIUM_NET_ENDPOINT_URL_HPP
