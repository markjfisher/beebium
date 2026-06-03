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

// Cross-platform: the host-serial transmit path must never block the emulation
// thread, even when the peer stops reading. A stuck HostSerialPort double lets
// us assert that without timing-sensitive plumbing.

#include "HostSerialEndpoint.hpp"

#include <beebium/serial/HostSerialPort.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>

using namespace beebium;

namespace {

// A port whose kernel buffer is permanently full: write() always accepts zero
// bytes (the EAGAIN path) and read() never delivers anything. The writer thread
// can never make progress, modelling a peer that has stopped reading.
class StuckPort final : public serial::HostSerialPort {
public:
    serial::ReadResult read(std::span<std::uint8_t> /*buffer*/) override {
        // Mimic the real port's ~100ms blocking timeout, cheaply, so the reader
        // thread does not busy-spin.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        serial::ReadResult r;
        r.would_block = true;
        return r;  // no data, no error
    }

    serial::WriteResult write(std::span<const std::uint8_t> /*bytes*/) override {
        serial::WriteResult w;
        w.bytes = 0;  // buffer full forever; no error (EAGAIN)
        return w;
    }

    bool is_open() const override { return open_.load(std::memory_order_relaxed); }
    void close() override { open_.store(false, std::memory_order_relaxed); }
    std::string_view open_error() const noexcept override { return {}; }

private:
    std::atomic<bool> open_{true};
};

// A trivially-open port: reads time out, writes succeed. Used as the
// SerialFactory output so a reopen lands on a healthy port.
class OpenPort final : public serial::HostSerialPort {
public:
    serial::ReadResult read(std::span<std::uint8_t> /*buffer*/) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        serial::ReadResult r;
        r.would_block = true;
        return r;
    }
    serial::WriteResult write(std::span<const std::uint8_t> bytes) override {
        serial::WriteResult w;
        w.bytes = bytes.size();
        return w;
    }
    bool is_open() const override { return open_.load(std::memory_order_relaxed); }
    void close() override { open_.store(false, std::memory_order_relaxed); }
    std::string_view open_error() const noexcept override { return {}; }

private:
    std::atomic<bool> open_{true};
};

}  // namespace

TEST_CASE("HostSerialEndpoint back-pressures a stuck peer without blocking",
          "[serial][host-serial]") {
    serial::HostSerialEndpoint endpoint(std::make_unique<StuckPort>());
    CHECK(endpoint.accepts_more());  // clear to send initially

    // Emulation-thread role: transmit far more than the queue can hold. The
    // writer thread can never drain it (stuck peer), but add_byte must return
    // immediately every time -- it never blocks on the OS write.
    const std::size_t flood = endpoint.tx_hard_cap() + 4096;
    for (std::size_t i = 0; i < flood; ++i) {
        endpoint.add_byte(0x55);
    }

    // The queue is bounded and back-pressure is asserted, so the ULA would hold
    // /CTS and stall the guest -- not the host.
    CHECK_FALSE(endpoint.accepts_more());
    CHECK(endpoint.tx_pending() <= endpoint.tx_hard_cap());
    CHECK(endpoint.tx_dropped() > 0);
}

TEST_CASE("HostSerialEndpoint re-points to a new device on request_reopen",
          "[serial][host-serial]") {
    int factory_calls = 0;
    std::string last_path;
    int last_baud = 0;
    std::atomic<int> state_changes{0};

    serial::HostSerialEndpoint::Options options;
    options.device_path = "/dev/old";
    options.baud = 9600;
    options.factory = [&](const std::string& path, int baud)
        -> std::unique_ptr<serial::HostSerialPort> {
        ++factory_calls;
        last_path = path;
        last_baud = baud;
        return std::make_unique<OpenPort>();
    };
    options.on_async_state_change = [&] { ++state_changes; };

    serial::HostSerialEndpoint endpoint(std::make_unique<OpenPort>(), std::move(options));

    auto before = endpoint.ui_snapshot();
    CHECK(before.device_path == "/dev/old");
    CHECK(before.baud == 9600);
    CHECK(before.serial_open);

    endpoint.request_reopen("/dev/new", 115200);
    // The reopen is applied on the emulation thread's next has_data() tick.
    endpoint.has_data();

    CHECK(factory_calls == 1);
    CHECK(last_path == "/dev/new");
    CHECK(last_baud == 115200);

    auto after = endpoint.ui_snapshot();
    CHECK(after.device_path == "/dev/new");
    CHECK(after.baud == 115200);
    CHECK(after.serial_open);
    CHECK(state_changes.load() >= 1);  // the reopen fired the async-state callback
}

TEST_CASE("HostSerialEndpoint request_reopen is a no-op without a factory",
          "[serial][host-serial]") {
    serial::HostSerialEndpoint endpoint(std::make_unique<OpenPort>());  // no factory
    endpoint.request_reopen("/dev/ignored", 9600);
    endpoint.has_data();
    // Default snapshot path stays empty; the original port is untouched.
    CHECK(endpoint.ui_snapshot().device_path.empty());
    CHECK(endpoint.is_open());
}
