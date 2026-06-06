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

#include "LoopbackTcpServer.hpp"

#include <beebium/net/TcpClientSerialPort.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <span>
#include <string>
#include <vector>

using namespace beebium;
using namespace std::chrono_literals;

namespace {

// Read until `n` bytes arrive or the deadline passes. read() itself paces the
// loop (it blocks up to ~100ms per call), so this is feedback-driven, not a
// dead-reckoning sleep.
std::vector<std::uint8_t> read_until(net::TcpClientSerialPort& port, std::size_t n,
                                     std::chrono::milliseconds budget) {
    std::vector<std::uint8_t> out;
    std::array<std::uint8_t, 64> buf{};
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (out.size() < n && std::chrono::steady_clock::now() < deadline) {
        serial::ReadResult r = port.read(std::span<std::uint8_t>(buf.data(), buf.size()));
        if (r.error) break;
        for (std::size_t i = 0; i < r.bytes; ++i) out.push_back(buf[i]);
    }
    return out;
}

// An ephemeral port that nothing is listening on (bind, read the port, close).
std::uint16_t closed_port() {
    net::ensure_winsock_initialized();
    net::socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    std::uint16_t port = ntohs(bound.sin_port);
    net::close_socket(s);
    return port;
}

}  // namespace

TEST_CASE("TcpClientSerialPort connects and round-trips bytes", "[net][tcp]") {
    test::LoopbackTcpServer server;  // echoes
    net::TcpClientSerialPort port("127.0.0.1", server.port());

    REQUIRE(port.is_open());

    const std::array<std::uint8_t, 5> msg{'H', 'e', 'l', 'l', 'o'};
    serial::WriteResult w = port.write(std::span<const std::uint8_t>(msg.data(), msg.size()));
    REQUIRE_FALSE(w.error);
    REQUIRE(w.bytes == msg.size());

    std::vector<std::uint8_t> echoed = read_until(port, msg.size(), 2s);
    REQUIRE(echoed.size() == msg.size());
    CHECK(std::equal(echoed.begin(), echoed.end(), msg.begin()));
}

TEST_CASE("TcpClientSerialPort close() unblocks a blocked read", "[net][tcp]") {
    test::LoopbackTcpServer server;
    net::TcpClientSerialPort port("127.0.0.1", server.port());
    REQUIRE(port.is_open());

    // A reader parked in read() (no data is coming) must return with error once
    // close() shuts the socket down.
    auto reader = std::async(std::launch::async, [&port] {
        std::array<std::uint8_t, 16> buf{};
        for (;;) {
            serial::ReadResult r =
                port.read(std::span<std::uint8_t>(buf.data(), buf.size()));
            if (r.error) return true;
        }
    });

    port.close();

    REQUIRE(reader.wait_for(2s) == std::future_status::ready);
    CHECK(reader.get() == true);
    CHECK_FALSE(port.is_open());
}

TEST_CASE("TcpClientSerialPort reports a failed connection", "[net][tcp]") {
    net::TcpClientSerialPort port("127.0.0.1", closed_port(), 1s);
    CHECK_FALSE(port.is_open());
    CHECK_FALSE(port.open_error().empty());
}

TEST_CASE("TcpClientSerialPort sees a peer disconnect as a read error", "[net][tcp]") {
    test::LoopbackTcpServer server;
    net::TcpClientSerialPort port("127.0.0.1", server.port());
    REQUIRE(port.is_open());

    // Make sure the server has accepted before dropping the client.
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!server.has_client() && std::chrono::steady_clock::now() < deadline) {
        const std::array<std::uint8_t, 1> ping{0x00};
        port.write(std::span<const std::uint8_t>(ping.data(), ping.size()));
        std::array<std::uint8_t, 16> buf{};
        port.read(std::span<std::uint8_t>(buf.data(), buf.size()));
    }
    REQUIRE(server.has_client());

    server.close_client();

    // The next reads should drain any echoed ping then surface the disconnect.
    bool saw_error = false;
    const auto err_deadline = std::chrono::steady_clock::now() + 2s;
    std::array<std::uint8_t, 16> buf{};
    while (std::chrono::steady_clock::now() < err_deadline) {
        serial::ReadResult r = port.read(std::span<std::uint8_t>(buf.data(), buf.size()));
        if (r.error) {
            saw_error = true;
            break;
        }
    }
    CHECK(saw_error);
}
