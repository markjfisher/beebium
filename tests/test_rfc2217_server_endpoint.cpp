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

// End-to-end RFC 2217 over real TCP: the real client endpoint connects to the
// real server endpoint and they exchange data through the full Telnet/COM-PORT
// negotiation -- the strongest in-process check that both halves of the codec
// and both transports interoperate.

#include "Rfc2217ClientEndpoint.hpp"
#include "Rfc2217ServerEndpoint.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

using namespace beebium;
using namespace std::chrono_literals;

namespace {

template <typename Predicate>
bool wait_until(Predicate pred, std::chrono::milliseconds budget = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return pred();
}

bool drain_one(rfc2217::Rfc2217ServerEndpoint& ep, std::uint8_t& out,
               std::chrono::milliseconds budget = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ep.has_data()) { out = ep.next_byte(); return true; }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

bool drain_one(rfc2217::Rfc2217ClientEndpoint& ep, std::uint8_t& out,
               std::chrono::milliseconds budget = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ep.has_data()) { out = ep.next_byte(); return true; }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

}  // namespace

TEST_CASE("RFC2217 client and server endpoints interoperate over TCP",
          "[rfc2217][endpoint]") {
    rfc2217::Rfc2217ServerEndpoint::Options server_opts;
    server_opts.bind = "127.0.0.1";
    server_opts.port = 0;  // ephemeral
    rfc2217::Rfc2217ServerEndpoint server(std::move(server_opts));
    REQUIRE(server.is_listening());
    const std::uint16_t port = server.local_port();
    REQUIRE(port != 0);

    rfc2217::Rfc2217ClientEndpoint::Options client_opts;
    client_opts.host = "127.0.0.1";
    client_opts.port = port;
    client_opts.baud = 9600;
    rfc2217::Rfc2217ClientEndpoint client(std::move(client_opts));

    REQUIRE(wait_until([&] { return client.connected() && server.connected(); }));
    REQUIRE(wait_until([&] { return client.option_negotiated(); }));

    // Client's BBC -> server's BBC (a literal 0xFF must survive the framing).
    client.add_byte('A');
    client.add_byte(0xFF);
    std::uint8_t b = 0;
    REQUIRE(drain_one(server, b));
    CHECK(b == 'A');
    REQUIRE(drain_one(server, b));
    CHECK(b == 0xFF);

    // Server's BBC -> client's BBC.
    server.add_byte('Z');
    REQUIRE(drain_one(client, b));
    CHECK(b == 'Z');
}

TEST_CASE("RFC2217 server endpoint reports a bind failure", "[rfc2217][endpoint]") {
    // 192.0.2.0/24 (RFC 5737 TEST-NET-1) is never assigned to the host, so a
    // bind to it deterministically fails.
    rfc2217::Rfc2217ServerEndpoint::Options opts;
    opts.bind = "192.0.2.1";
    opts.port = 4001;
    rfc2217::Rfc2217ServerEndpoint server(std::move(opts));
    CHECK_FALSE(server.is_listening());
}
