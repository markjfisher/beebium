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

#pragma once

#include <cstdint>
#include <deque>

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
};

// In-memory source/sink used by unit tests and as a simple loopback. Bytes
// written via add_byte() become readable through has_data()/next_byte(),
// so connecting it as both source and sink echoes transmitted bytes back to
// the receiver.
class LoopbackSerialEndpoint final : public SerialDataSource,
                                     public SerialDataSink {
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

}  // namespace beebium
