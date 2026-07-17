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
#include <mutex>
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
// Pure in-process; no real I/O.
//
// Every member is serialised under mutex_, because the object this stands in
// for -- a kernel serial buffer -- is itself safe to fill from one thread
// while another drains it. PiconetBackend starts its reader thread from its
// constructor, so that thread is already inside read() by the time a test
// stages a chunk or inspects a write: the test thread and the reader thread
// touch this object concurrently for its whole lifetime, and no ordering
// discipline on the test's part can prevent it.
class MockPiconetSerial : public SerialPort {
public:
    MockPiconetSerial() = default;

    // ---- SerialPort interface ----

    ReadResult read(std::span<std::uint8_t> buffer) override {
        const std::lock_guard<std::mutex> lock(mutex_);
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
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) {
            return WriteResult{0, true};
        }
        writes_.emplace_back(bytes.begin(), bytes.end());
        return WriteResult{bytes.size(), false};
    }

    bool is_open() const override {
        const std::lock_guard<std::mutex> lock(mutex_);
        return open_;
    }

    void close() override {
        const std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }

    // Returns by value rather than referring into open_error_, which another
    // thread may reassign via set_open_error(). The SerialPort interface fixes
    // the string_view return type, so the storage must outlive the call: hand
    // back a view of a string with static storage duration, refreshed under the
    // lock. Tests read this from one thread at a time.
    std::string_view open_error() const noexcept override {
        const std::lock_guard<std::mutex> lock(mutex_);
        static thread_local std::string snapshot;
        snapshot = open_error_;
        return snapshot;
    }

    // ---- Test control: stage inbound bytes ----

    // Push bytes that the next read() call (or several calls, depending on
    // buffer size) will deliver. Multiple chunks are honoured in order;
    // each chunk is a separate read() boundary, which lets tests force
    // partial-line conditions.
    void stage_read_chunk(std::string_view text) {
        const std::lock_guard<std::mutex> lock(mutex_);
        read_chunks_.emplace_back(text.begin(), text.end());
    }
    void stage_read_chunk(std::span<const std::uint8_t> bytes) {
        const std::lock_guard<std::mutex> lock(mutex_);
        read_chunks_.emplace_back(bytes.begin(), bytes.end());
    }

    // ---- Test control: connection state ----

    void set_open(bool o) {
        const std::lock_guard<std::mutex> lock(mutex_);
        open_ = o;
    }

    // Inject a synthetic OS-level error so tests can verify the
    // PiconetBackend / PiconetUi paths that surface
    // serial->open_error() to the Indicator. Default is empty.
    void set_open_error(std::string err) {
        const std::lock_guard<std::mutex> lock(mutex_);
        open_error_ = std::move(err);
    }

    // ---- Test inspection: captured writes ----

    // Number of write() calls observed.
    std::size_t write_count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return writes_.size();
    }

    // Get the bytes from the i-th write() as a string (PiconetBackend writes
    // ASCII-safe protocol lines).
    std::string write_as_string(std::size_t i) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto& w = writes_.at(i);
        return std::string(w.begin(), w.end());
    }

    // All writes concatenated into one string; useful when PiconetBackend
    // makes several small writes per command.
    std::string all_writes_concatenated() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::string out;
        for (const auto& w : writes_) {
            out.append(w.begin(), w.end());
        }
        return out;
    }

    void clear_writes() {
        const std::lock_guard<std::mutex> lock(mutex_);
        writes_.clear();
    }

private:
    mutable std::mutex mutex_;
    bool open_ = true;
    std::string open_error_;
    std::deque<std::vector<std::uint8_t>> read_chunks_;
    std::vector<std::vector<std::uint8_t>> writes_;
};

}  // namespace beebium::piconet::test
