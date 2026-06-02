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

#include "SerialDevice.hpp"
#include "SerialPort.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

namespace beebium::serial {

// Bridges the Serial ULA to a host SerialPort (a PTY master or an opened
// device path), exposing the SerialDataSource / SerialDataSink interfaces the
// ULA expects.
//
// A dedicated reader thread does the blocking-style reads from the OS port and
// pushes received bytes into a mutex-protected queue, which the ULA drains on
// the emulation thread (device -> Beeb). Transmitted bytes are written straight
// to the port on the emulation thread (Beeb -> device); per the SerialPort
// threading contract the emulation thread is the sole writer, so no write lock
// is needed.
//
// NOTE: bytes are currently written one at a time as the ULA produces them.
// b2 found that some peers need transmitted frames coalesced (a short
// debounce) to avoid packet splitting; that tuning, if needed, belongs here in
// add_byte()/a flush timer and is deliberately left out for now.
class HostSerialEndpoint final : public beebium::SerialPortDevice {
public:
    explicit HostSerialEndpoint(std::unique_ptr<SerialPort> port)
        : port_(std::move(port)) {
        if (port_ && port_->is_open()) {
            reader_ = std::thread([this] { reader_loop(); });
        }
    }

    ~HostSerialEndpoint() override {
        stop_.store(true, std::memory_order_release);
        if (port_) {
            port_->close();  // unblock a pending read() so the thread can exit
        }
        if (reader_.joinable()) {
            reader_.join();
        }
    }

    HostSerialEndpoint(const HostSerialEndpoint&) = delete;
    HostSerialEndpoint& operator=(const HostSerialEndpoint&) = delete;

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
        if (port_) {
            std::array<std::uint8_t, 1> one{value};
            port_->write(std::span<const std::uint8_t>(one.data(), one.size()));
        }
    }

    // True while the underlying port is usable.
    bool is_open() const { return port_ && port_->is_open(); }

private:
    void reader_loop() {
        std::array<std::uint8_t, 256> buf{};
        while (!stop_.load(std::memory_order_acquire)) {
            if (!port_->is_open()) {
                break;
            }
            ReadResult r = port_->read(std::span<std::uint8_t>(buf.data(), buf.size()));
            if (r.error) {
                break;
            }
            if (r.bytes > 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                for (std::size_t i = 0; i < r.bytes; ++i) {
                    rx_.push_back(buf[i]);
                }
            }
            // would_block: the read already waited ~100ms, so just loop.
        }
    }

    std::unique_ptr<SerialPort> port_;
    std::thread reader_;
    std::atomic<bool> stop_{false};

    std::mutex mutex_;
    std::deque<std::uint8_t> rx_;  // device -> Beeb
};

}  // namespace beebium::serial
