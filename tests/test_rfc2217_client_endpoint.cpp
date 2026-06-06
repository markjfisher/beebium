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

// In-process integration test for the RFC 2217 client endpoint: an in-process
// RFC 2217 "echo server" speaks the SERVER half of the same codec (answering the
// Telnet negotiation and echoing data), so the full client<->server exchange --
// negotiation, baud, data with IAC escaping -- runs end to end without pyserial.

#include "Rfc2217ClientEndpoint.hpp"
#include "Rfc2217Codec.hpp"

#include <beebium/net/TcpServerSerialPort.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
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

// An RFC 2217 access server that answers the Telnet negotiation and echoes the
// data the client transmits straight back (re-framed). Enough to exercise the
// client endpoint's full path in process.
class Rfc2217EchoServer {
public:
    Rfc2217EchoServer()
        : server_("127.0.0.1", 0), codec_(rfc2217::Rfc2217Codec::Role::Server) {
        thread_ = std::thread([this] { run(); });
    }
    ~Rfc2217EchoServer() {
        stop_.store(true);
        server_.close();
        if (thread_.joinable()) thread_.join();
    }
    std::uint16_t port() const { return server_.local_port(); }

private:
    void run() {
        std::array<std::uint8_t, 256> buf{};
        std::vector<std::uint8_t> data, reply;
        std::vector<rfc2217::ComPortCommand> cmds;
        while (!stop_.load()) {
            serial::ReadResult r =
                server_.read(std::span<std::uint8_t>(buf.data(), buf.size()));
            if (r.error) break;
            if (r.bytes == 0) continue;
            data.clear();
            reply.clear();
            cmds.clear();
            codec_.decode(std::span<const std::uint8_t>(buf.data(), r.bytes), data, cmds,
                          reply);
            if (!reply.empty()) {
                server_.write(std::span<const std::uint8_t>(reply.data(), reply.size()));
            }
            if (!data.empty()) {
                std::vector<std::uint8_t> echo;
                codec_.encode_data(std::span<const std::uint8_t>(data.data(), data.size()),
                                   echo);
                server_.write(std::span<const std::uint8_t>(echo.data(), echo.size()));
            }
        }
    }

    net::TcpServerSerialPort server_;
    rfc2217::Rfc2217Codec codec_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

std::vector<std::uint8_t> drain(rfc2217::Rfc2217ClientEndpoint& ep, std::size_t n,
                                std::chrono::milliseconds budget = 3s) {
    std::vector<std::uint8_t> out;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (out.size() < n && std::chrono::steady_clock::now() < deadline) {
        if (ep.has_data()) out.push_back(ep.next_byte());
        else std::this_thread::sleep_for(2ms);
    }
    return out;
}

}  // namespace

TEST_CASE("Rfc2217ClientEndpoint negotiates and round-trips data", "[rfc2217][endpoint]") {
    Rfc2217EchoServer server;
    rfc2217::Rfc2217ClientEndpoint::Options options;
    options.host = "127.0.0.1";
    options.port = server.port();
    options.baud = 9600;
    rfc2217::Rfc2217ClientEndpoint ep(std::move(options));

    REQUIRE(wait_until([&] { return ep.connected(); }));
    REQUIRE(wait_until([&] { return ep.option_negotiated(); }));
    CHECK(ep.accepts_more());

    // Transmit, including a literal 0xFF (must survive IAC IAC escaping).
    for (std::uint8_t b : {std::uint8_t{'A'}, std::uint8_t{0xFF}, std::uint8_t{'B'}}) {
        ep.add_byte(b);
    }
    std::vector<std::uint8_t> echoed = drain(ep, 3);
    REQUIRE(echoed.size() == 3);
    CHECK(echoed[0] == 'A');
    CHECK(echoed[1] == 0xFF);
    CHECK(echoed[2] == 'B');
}

TEST_CASE("Rfc2217ClientEndpoint holds /CTS until connected", "[rfc2217][endpoint]") {
    // A dead port: never connects, so the device is never clear to send.
    rfc2217::Rfc2217ClientEndpoint::Options options;
    options.host = "127.0.0.1";
    options.port = 1;  // refused
    rfc2217::Rfc2217ClientEndpoint ep(std::move(options));
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(ep.connected());
    CHECK_FALSE(ep.accepts_more());
}
