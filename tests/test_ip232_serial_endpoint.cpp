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

// Integration tests for the IP232 endpoint against an in-process TCP echo
// server. A plain echo is enough to exercise the full ip232 round-trip: the
// endpoint encodes outbound, the server echoes the wire bytes, and the endpoint
// decodes them back -- so the escape handling is covered end to end without a
// real tcpser. (Exact wire-byte correctness is the codec golden-vector test.)

#include "Ip232SerialEndpoint.hpp"
#include "LoopbackTcpServer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

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

// Drain up to `n` received bytes from the endpoint, polling until they arrive.
std::vector<std::uint8_t> drain(ip232::Ip232SerialEndpoint& ep, std::size_t n,
                                std::chrono::milliseconds budget = 3s) {
    std::vector<std::uint8_t> out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (out.size() < n && std::chrono::steady_clock::now() < deadline) {
        if (ep.has_data()) {
            out.push_back(ep.next_byte());
        } else {
            std::this_thread::sleep_for(2ms);
        }
    }
    return out;
}

ip232::Ip232SerialEndpoint::Options opts(std::uint16_t port, bool raw) {
    ip232::Ip232SerialEndpoint::Options o;
    o.host = "127.0.0.1";
    o.port = port;
    o.raw = raw;
    return o;
}

}  // namespace

TEST_CASE("Ip232SerialEndpoint connects and round-trips bytes (ip232 mode)",
          "[ip232][endpoint]") {
    test::LoopbackTcpServer server;  // echoes the wire bytes
    ip232::Ip232SerialEndpoint ep(opts(server.port(), /*raw=*/false));

    REQUIRE(wait_until([&] { return ep.connected(); }));

    // Transmit, including a literal 0xFF which must survive the escape doubling.
    for (std::uint8_t b : {std::uint8_t{'A'}, std::uint8_t{0xFF}, std::uint8_t{'B'}}) {
        ep.add_byte(b);
    }

    std::vector<std::uint8_t> received = drain(ep, 3);
    REQUIRE(received.size() == 3);
    CHECK(received[0] == 'A');
    CHECK(received[1] == 0xFF);
    CHECK(received[2] == 'B');
}

TEST_CASE("Ip232SerialEndpoint holds /CTS until connected", "[ip232][endpoint]") {
    test::LoopbackTcpServer server;
    ip232::Ip232SerialEndpoint ep(opts(server.port(), /*raw=*/false));

    // Before the connection is up the device is not clear to send.
    // (It connects quickly, so just assert it becomes ready.)
    REQUIRE(wait_until([&] { return ep.connected(); }));
    CHECK(ep.accepts_more());
}

TEST_CASE("Ip232SerialEndpoint raw mode connects and disconnects on RTS",
          "[ip232][endpoint]") {
    test::LoopbackTcpServer server;
    ip232::Ip232SerialEndpoint ep(opts(server.port(), /*raw=*/true));

    // RTS starts deasserted: no connection.
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(ep.connected());

    ep.set_rts(true);
    REQUIRE(wait_until([&] { return ep.connected() && server.has_client(); }));

    ep.set_rts(false);
    REQUIRE(wait_until([&] { return !ep.connected(); }));
}

TEST_CASE("Ip232SerialEndpoint reconnects after a peer drop (ip232 mode)",
          "[ip232][endpoint]") {
    test::LoopbackTcpServer server;
    ip232::Ip232SerialEndpoint ep(opts(server.port(), /*raw=*/false));

    REQUIRE(wait_until([&] { return ep.connected() && server.has_client(); }));

    server.close_client();
    REQUIRE(wait_until([&] { return !ep.connected(); }));

    // ip232 mode keeps a persistent connection, so it reconnects.
    REQUIRE(wait_until([&] { return ep.connected() && server.has_client(); }, 5s));
}
