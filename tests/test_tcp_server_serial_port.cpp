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

#include <beebium/net/TcpClientSerialPort.hpp>
#include <beebium/net/TcpServerSerialPort.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
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

// Pumps a TcpServerSerialPort's read() on a background thread (driving accept +
// recv) and collects everything received, so the test thread can assert on it.
class ServerPump {
public:
    explicit ServerPump(net::TcpServerSerialPort& server) : server_(server) {
        thread_ = std::thread([this] {
            std::array<std::uint8_t, 256> buf{};
            while (!stop_.load()) {
                serial::ReadResult r =
                    server_.read(std::span<std::uint8_t>(buf.data(), buf.size()));
                if (r.error) break;
                if (r.bytes > 0) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    received_.insert(received_.end(), buf.begin(), buf.begin() + r.bytes);
                }
            }
        });
    }
    ~ServerPump() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    std::vector<std::uint8_t> received() {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_;
    }

private:
    net::TcpServerSerialPort& server_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<std::uint8_t> received_;
    std::thread thread_;
};

std::vector<std::uint8_t> read_until(net::TcpClientSerialPort& port, std::size_t n,
                                     std::chrono::milliseconds budget = 3s) {
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

}  // namespace

TEST_CASE("TcpServerSerialPort accepts a client and round-trips bytes", "[net][tcp]") {
    net::TcpServerSerialPort server("127.0.0.1", 0);
    REQUIRE(server.is_open());
    const std::uint16_t port = server.local_port();
    REQUIRE(port != 0);
    CHECK_FALSE(server.is_connected());

    ServerPump pump(server);
    net::TcpClientSerialPort client("127.0.0.1", port);
    REQUIRE(client.is_open());
    REQUIRE(wait_until([&] { return server.is_connected(); }));

    // client -> server
    const std::array<std::uint8_t, 2> hi{'H', 'i'};
    client.write(std::span<const std::uint8_t>(hi.data(), hi.size()));
    REQUIRE(wait_until([&] { return pump.received().size() >= 2; }));
    auto got = pump.received();
    CHECK(got[0] == 'H');
    CHECK(got[1] == 'i');

    // server -> client
    const std::array<std::uint8_t, 2> yo{'Y', 'o'};
    server.write(std::span<const std::uint8_t>(yo.data(), yo.size()));
    auto echoed = read_until(client, 2);
    REQUIRE(echoed.size() == 2);
    CHECK(echoed[0] == 'Y');
    CHECK(echoed[1] == 'o');
}

TEST_CASE("TcpServerSerialPort rejects a second client", "[net][tcp]") {
    net::TcpServerSerialPort server("127.0.0.1", 0);
    const std::uint16_t port = server.local_port();
    ServerPump pump(server);

    net::TcpClientSerialPort first("127.0.0.1", port);
    REQUIRE(wait_until([&] { return server.is_connected(); }));

    net::TcpClientSerialPort second("127.0.0.1", port);
    // The server stays attached to the first client; the second is dropped, so a
    // read on it returns an error.
    bool second_dropped = false;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    std::array<std::uint8_t, 16> buf{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (second.read(std::span<std::uint8_t>(buf.data(), buf.size())).error) {
            second_dropped = true;
            break;
        }
    }
    CHECK(second_dropped);
    CHECK(server.is_connected());  // still serving the first client
}

TEST_CASE("TcpServerSerialPort keeps serving after a client disconnects", "[net][tcp]") {
    net::TcpServerSerialPort server("127.0.0.1", 0);
    const std::uint16_t port = server.local_port();
    ServerPump pump(server);

    {
        net::TcpClientSerialPort first("127.0.0.1", port);
        REQUIRE(wait_until([&] { return server.is_connected(); }));
    }  // first client destroyed -> disconnects
    REQUIRE(wait_until([&] { return !server.is_connected(); }));

    net::TcpClientSerialPort second("127.0.0.1", port);
    REQUIRE(wait_until([&] { return server.is_connected(); }));
}
