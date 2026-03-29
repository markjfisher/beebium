// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SCSI_BUS_EVENT_BUFFER_HPP
#define BEEBIUM_SCSI_BUS_EVENT_BUFFER_HPP

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace beebium {

// A structured SCSI bus event for diagnostic observation.
struct ScsiInternalEvent {
    enum class Type {
        PhaseChange,
        Selection,
        Command,
        DataTransfer,
        Status,
        RegisterAccess,
    };

    Type type;

    // PhaseChange
    std::string from_phase;
    std::string to_phase;

    // Selection
    uint8_t target_id = 0xFF;
    bool selection_success = false;

    // Command
    uint8_t opcode = 0;
    std::string opcode_name;
    std::vector<uint8_t> cdb;
    uint32_t lba = 0;
    uint32_t block_count = 0;

    // DataTransfer
    std::string direction;      // "IN" or "OUT"
    uint32_t bytes_expected = 0;
    uint32_t bytes_transferred = 0;
    bool transfer_complete = false;

    // Status
    uint8_t status_byte = 0;
    std::string status_name;
    uint8_t message_byte = 0;

    // RegisterAccess
    std::string operation;      // "READ" or "WRITE"
    uint8_t register_index = 0;
    std::string register_name;
    uint8_t value = 0;
    std::string phase;          // current phase at time of access
};

// Thread-safe bounded event buffer.
//
// The emulation thread pushes events. The gRPC streaming thread pops them.
// When the buffer is full, oldest events are dropped (non-blocking producer).
// The consumer blocks until events are available or the buffer is closed.
class ScsiBusEventBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 4096;

    explicit ScsiBusEventBuffer(size_t capacity = DEFAULT_CAPACITY)
        : capacity_(capacity) {}

    // Push an event (called from the emulation thread).
    // If the buffer is full, the oldest event is dropped.
    // If the buffer is closed, the event is silently discarded.
    void push(ScsiInternalEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        if (events_.size() >= capacity_) {
            events_.pop_front();
            ++dropped_count_;
        }
        events_.push_back(std::move(event));
        cv_.notify_one();
    }

    // Pop an event (called from the gRPC thread).
    // Blocks until an event is available or the buffer is closed.
    // Returns false if the buffer is closed and empty.
    bool pop(ScsiInternalEvent& event) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !events_.empty() || closed_; });
        if (events_.empty()) return false;
        event = std::move(events_.front());
        events_.pop_front();
        return true;
    }

    // Close the buffer. Wakes any blocked consumer.
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    size_t dropped_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_count_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<ScsiInternalEvent> events_;
    size_t capacity_;
    size_t dropped_count_ = 0;
    bool closed_ = false;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_BUS_EVENT_BUFFER_HPP
