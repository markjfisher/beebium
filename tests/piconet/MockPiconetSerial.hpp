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

#include "beebium/econet/piconet/SerialPort.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::piconet::test {

// Scripted serial-port test double for unit tests of PiconetBackend's
// protocol layer. Captures every write() (so tests can assert exact wire
// content) and serves bytes from a pre-staged read script (so tests can
// simulate inbound serial traffic, including adversarial chunking across
// reads).
//
// Pure in-process; no real I/O. Safe to use in any thread provided the
// caller observes the SerialPort threading contract (writes from one
// thread, reads from another, no concurrent writes).
class MockPiconetSerial : public SerialPort {
public:
    MockPiconetSerial() = default;

    // ---- SerialPort interface ----

    ReadResult read(std::span<std::uint8_t> buffer) override {
        if (!open_) {
            return ReadResult{0, false, true};
        }
        if (read_chunks_.empty()) {
            return ReadResult{0, true, false};  // would_block
        }
        auto& chunk = read_chunks_.front();
        const std::size_t to_copy = std::min(buffer.size(), chunk.size());
        for (std::size_t i = 0; i < to_copy; ++i) {
            buffer[i] = chunk[i];
        }
        if (to_copy == chunk.size()) {
            read_chunks_.pop_front();
        } else {
            chunk.erase(chunk.begin(), chunk.begin() + to_copy);
        }
        return ReadResult{to_copy, false, false};
    }

    WriteResult write(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return WriteResult{0, true};
        }
        writes_.emplace_back(bytes.begin(), bytes.end());
        return WriteResult{bytes.size(), false};
    }

    bool is_open() const override { return open_; }

    void close() override { open_ = false; }

    std::string_view open_error() const noexcept override {
        return open_error_;
    }

    // ---- Test control: stage inbound bytes ----

    // Push bytes that the next read() call (or several calls, depending on
    // buffer size) will deliver. Multiple chunks are honoured in order;
    // each chunk is a separate read() boundary, which lets tests force
    // partial-line conditions.
    void stage_read_chunk(std::string_view text) {
        read_chunks_.emplace_back(text.begin(), text.end());
    }
    void stage_read_chunk(std::span<const std::uint8_t> bytes) {
        read_chunks_.emplace_back(bytes.begin(), bytes.end());
    }

    // ---- Test control: connection state ----

    void set_open(bool o) { open_ = o; }

    // Inject a synthetic OS-level error so tests can verify the
    // PiconetBackend / PiconetUi paths that surface
    // serial->open_error() to the Indicator. Default is empty.
    void set_open_error(std::string err) { open_error_ = std::move(err); }

    // ---- Test inspection: captured writes ----

    // Number of write() calls observed.
    std::size_t write_count() const { return writes_.size(); }

    // Get the bytes from the i-th write() as a string (PiconetBackend writes
    // ASCII-safe protocol lines).
    std::string write_as_string(std::size_t i) const {
        const auto& w = writes_.at(i);
        return std::string(w.begin(), w.end());
    }

    // All writes concatenated into one string; useful when PiconetBackend
    // makes several small writes per command.
    std::string all_writes_concatenated() const {
        std::string out;
        for (const auto& w : writes_) {
            out.append(w.begin(), w.end());
        }
        return out;
    }

    void clear_writes() { writes_.clear(); }

private:
    bool open_ = true;
    std::string open_error_;
    std::deque<std::vector<std::uint8_t>> read_chunks_;
    std::vector<std::vector<std::uint8_t>> writes_;
};

}  // namespace beebium::piconet::test
