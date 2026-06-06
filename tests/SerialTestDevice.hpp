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

#include <beebium/serial/SerialDevice.hpp>

#include <cstdint>
#include <deque>
#include <vector>

namespace beebium {

// A SerialPortDevice double for exercising the serial bit engine (ACIA / ULA /
// SerialSocket) without pulling in a production device extension. The concrete
// devices live in their extensions; this is the test fixture they would
// otherwise stand in for.
//
//   As a source: push_from_device() queues bytes the Beeb will receive.
//   As a sink:   transmitted bytes are captured in sent(); with echo enabled
//                they also feed the source queue, giving a loopback.
//   Flow control: set_accepts_more(false) models a device that has stopped
//                accepting, so the ULA asserts the ACIA's /CTS input.
//
// Single-threaded: every method runs on the test (emulation) thread, so no
// locking is needed.
class SerialTestDevice final : public SerialPortDevice {
public:
    explicit SerialTestDevice(bool echo = false) : echo_(echo) {}

    // --- SerialDataSource (device -> Beeb) ---
    bool has_data() override { return !rx_.empty(); }
    uint8_t next_byte() override {
        uint8_t value = rx_.front();
        rx_.pop_front();
        return value;
    }

    // --- SerialDataSink (Beeb -> device) ---
    void add_byte(uint8_t value) override {
        sent_.push_back(value);
        if (echo_) rx_.push_back(value);
    }
    bool accepts_more() override { return accepts_more_; }
    void set_rts(bool rts_asserted) override { rts_events_.push_back(rts_asserted); }

    // --- Test controls ---
    void push_from_device(uint8_t value) { rx_.push_back(value); }
    void set_accepts_more(bool ready) { accepts_more_ = ready; }
    const std::vector<uint8_t>& sent() const { return sent_; }
    bool source_empty() const { return rx_.empty(); }

    // RTS levels delivered by the ULA, in order (one per transition, plus the
    // baseline at attach).
    const std::vector<bool>& rts_events() const { return rts_events_; }

private:
    std::deque<uint8_t> rx_;     // device -> Beeb
    std::vector<uint8_t> sent_;  // Beeb -> device (captured)
    std::vector<bool> rts_events_;
    bool echo_;
    bool accepts_more_ = true;
};

}  // namespace beebium
