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

#ifndef BEEBIUM_EXT_RPC_SERIAL_ENDPOINT_HPP
#define BEEBIUM_EXT_RPC_SERIAL_ENDPOINT_HPP

#include <beebium/serial/SerialDevice.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace beebium {

// The far end of the BBC's serial wire when the rpc-serial extension owns the
// port: a thread-safe, client-driven endpoint with two independent channels.
//
//   device -> Beeb (receive): the RpcSerial client injects bytes via inject();
//     the Serial ULA drains them through the SerialDataSource interface on the
//     emulation thread, so the Beeb receives them.
//   Beeb -> device (transmit): the Serial ULA appends transmitted bytes through
//     the SerialDataSink interface on the emulation thread; the client collects
//     them via drain().
//
// Both queues are mutex-protected so the gRPC service thread and the emulation
// thread can use the endpoint concurrently without a global pause. The queues
// are bounded so no client can leak memory or stall the emulator host: TX
// back-pressure is via /CTS (see accepts_more), RX via the accepted count
// returned by inject(). It is the serial analogue of the Econet TestBackend: a
// deterministic, in-process transport with no PTY or external device involved.
class RpcSerialEndpoint final : public SerialPortDevice {
public:
    // Queue bounds keep an undraining/over-injecting client from growing memory
    // unboundedly or stalling the emulator. TX back-pressure is via /CTS (see
    // accepts_more); RX via the accepted count returned by inject().
    static constexpr size_t kRxCapacity = 4096;      // device -> Beeb cap
    static constexpr size_t kTxBackPressure = 4096;  // assert /CTS at/above
    static constexpr size_t kTxHardCap = kTxBackPressure + 64;  // absolute TX cap

    // --- SerialDataSource (device -> Beeb), called on the emulation thread ---
    bool has_data() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return !rx_.empty();
    }

    uint8_t next_byte() override {
        std::lock_guard<std::mutex> lock(mutex_);
        uint8_t value = rx_.front();
        rx_.pop_front();
        return value;
    }

    // --- SerialDataSink (Beeb -> device), called on the emulation thread ---
    void add_byte(uint8_t value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tx_.size() >= kTxHardCap) {
            // Client not draining AND guest ignoring /CTS (TDRE): a real ACIA
            // would lose data here too. Account it; never grow without bound.
            ++tx_dropped_;
            return;
        }
        tx_.push_back(value);
    }

    // Flow control (/CTS): not clear to send once tx_ reaches the back-pressure
    // mark, so the guest's transmit loop stalls until the client drains.
    bool accepts_more() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return tx_.size() < kTxBackPressure;
    }

    // --- Client side (gRPC thread) ---

    // Queue bytes for the Beeb to receive. Returns the number accepted, which is
    // less than len when the receive queue is full (the client retries the
    // unaccepted tail later). Never blocks.
    size_t inject(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t space = rx_.size() >= kRxCapacity ? 0 : kRxCapacity - rx_.size();
        const size_t accepted = std::min(len, space);
        for (size_t i = 0; i < accepted; ++i) rx_.push_back(data[i]);
        return accepted;
    }

    // Collect up to max_bytes that the Beeb has transmitted (0 = all available).
    std::vector<uint8_t> drain(size_t max_bytes = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t n = (max_bytes == 0 || max_bytes > tx_.size()) ? tx_.size() : max_bytes;
        std::vector<uint8_t> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            out.push_back(tx_.front());
            tx_.pop_front();
        }
        return out;
    }

    // Pending counts (bytes waiting in each direction).
    size_t rx_pending() {
        std::lock_guard<std::mutex> lock(mutex_);
        return rx_.size();
    }
    size_t tx_pending() {
        std::lock_guard<std::mutex> lock(mutex_);
        return tx_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        rx_.clear();
        tx_.clear();
    }

    // Bytes dropped because tx_ hit the hard cap (client not draining and the
    // guest ignoring /CTS). Exposed for diagnostics and tests.
    size_t tx_dropped() {
        std::lock_guard<std::mutex> lock(mutex_);
        return tx_dropped_;
    }

private:
    std::mutex mutex_;
    std::deque<uint8_t> rx_;  // device -> Beeb
    std::deque<uint8_t> tx_;  // Beeb -> device
    size_t tx_dropped_ = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_RPC_SERIAL_ENDPOINT_HPP
