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
#include <span>
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
