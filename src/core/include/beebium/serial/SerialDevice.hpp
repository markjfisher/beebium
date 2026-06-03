// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace beebium {

// Byte-oriented serial transport seam.
//
// The SerialUla serialises ACIA transmit bits into whole bytes and pushes them
// to a SerialDataSink (Beeb -> device), and pulls whole bytes from a
// SerialDataSource (device -> Beeb) and shifts them back into the ACIA as bits.
//
// These interfaces decouple the bit-level serial engine in the emulator core
// from the host transport (e.g. a PTY/serial bridge to fujinet-nio), which
// lives in the application/server layer. Implementations that bridge to a
// background I/O thread must be internally thread-safe; the methods here are
// called from the emulation thread.

// Device -> Beeb (the ULA reads received bytes from here).
class SerialDataSource {
public:
    virtual ~SerialDataSource() = default;

    // True if a byte is available to receive.
    virtual bool has_data() = 0;

    // Remove and return the next received byte. Only call when has_data().
    virtual uint8_t next_byte() = 0;
};

// Beeb -> device (the ULA writes transmitted bytes here).
class SerialDataSink {
public:
    virtual ~SerialDataSink() = default;

    // Accept one transmitted byte.
    virtual void add_byte(uint8_t value) = 0;

    // Flow control, modelling the /CTS line. Returns false when the device
    // cannot accept more transmitted bytes right now; the Serial ULA then
    // asserts the ACIA's /CTS input, which holds TDRE = 0 so the BBC's transmit
    // loop busy-waits (it stalls the emulated guest, never the emulator host).
    // Default: always clear to send.
    virtual bool accepts_more() { return true; }
};

// The seam a serial device attaches to: a single, bidirectional endpoint that
// the Serial ULA both receives from (device -> Beeb) and transmits to
// (Beeb -> device). This is the serial analogue of UserPortDevice /
// OneMHzBusDevice -- the one interface an attached device implements, whether a
// host-serial bridge or an emulated device (e.g. a FujiNet). It combines the two
// half-seams the bit engine uses internally; SerialSocket accepts a
// SerialPortDevice and wires it to the ULA as both source and sink.
class SerialPortDevice : public SerialDataSource, public SerialDataSink {
};

// In-memory source/sink used by unit tests and as a simple loopback. Bytes
// written via add_byte() become readable through has_data()/next_byte(),
// so connecting it as both source and sink echoes transmitted bytes back to
// the receiver.
class LoopbackSerialEndpoint final : public SerialPortDevice {
public:
    bool has_data() override { return !queue_.empty(); }

    uint8_t next_byte() override {
        uint8_t value = queue_.front();
        queue_.pop_front();
        return value;
    }

    void add_byte(uint8_t value) override { queue_.push_back(value); }

    // Test helpers
    bool empty() const { return queue_.empty(); }
    size_t size() const { return queue_.size(); }
    void clear() { queue_.clear(); }

    // Inject a byte as if it had arrived from the device (Beeb will receive it).
    void push_from_device(uint8_t value) { queue_.push_back(value); }

private:
    std::deque<uint8_t> queue_;
};

// Thread-safe, script-controlled endpoint with two independent channels:
//
//   device -> Beeb (receive): a client injects bytes via inject(); the Serial
//     ULA drains them through the SerialDataSource interface on the emulation
//     thread, so the Beeb receives them.
//   Beeb -> device (transmit): the Serial ULA appends transmitted bytes through
//     the SerialDataSink interface on the emulation thread; a client collects
//     them via drain().
//
// Both queues are mutex-protected so the gRPC service thread and the emulation
// thread can use the endpoint concurrently without a global pause. This is the
// serial analogue of the Econet TestBackend: a deterministic, in-process
// transport for scripts and tests, with no PTY or external device involved.
class ScriptableSerialEndpoint final : public SerialPortDevice {
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
