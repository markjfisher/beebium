// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "KeyboardMatrix.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

namespace beebium {

// Type-ahead queue for typing strings at machine speed
//
// This class manages a queue of strings to be typed into the emulated BBC Micro.
// It operates as a state machine that is driven by the main emulator loop via
// tick(). Each character is typed with proper timing, including SHIFT handling
// for uppercase letters and shifted symbols.
//
// Thread Safety:
// The enqueue(), empty(), pending_characters(), and clear() methods are
// thread-safe and can be called from any thread (e.g., gRPC service thread).
// The tick() method should only be called from the emulator thread.
//
// Typical usage:
//   1. gRPC service calls enqueue() to add text
//   2. Emulator main loop calls tick() each cycle
//   3. Queue types characters as fast as the machine can handle
//   4. Client polls status via empty()/pending_characters()
//
class TypeAheadQueue {
public:
    // Default cycles per key (100000 at 2MHz = 50ms, proven reliable minimum)
    static constexpr size_t DEFAULT_CYCLES_PER_KEY = 100000;

    explicit TypeAheadQueue(KeyboardMatrix& keyboard);

    // Enqueue a string to type (thread-safe)
    // All characters must be typeable on BBC keyboard.
    // Returns false if string contains unmappable characters.
    bool enqueue(std::string_view text, size_t cycles_per_key = DEFAULT_CYCLES_PER_KEY);

    // Called each machine cycle from the emulator main loop
    // Updates keyboard state based on timing.
    void tick();

    // Check if queue is empty (all strings typed) (thread-safe)
    bool empty() const;

    // Get number of characters remaining to type (thread-safe)
    size_t pending_characters() const;

    // Get number of strings in queue (thread-safe)
    size_t strings_queued() const;

    // Clear the queue, cancelling pending input (thread-safe)
    // Returns number of characters that were pending.
    size_t clear();

private:
    // State machine states
    enum class State {
        Idle,       // No pending input, waiting for next string
        KeyDown,    // Key pressed (with SHIFT if needed), counting down cycles
        KeyUp       // Key released, counting down cycles before next char
    };

    // Pending entry in the queue
    struct QueueEntry {
        std::string text;
        size_t cycles_per_key;
    };

    // Advance to next character or string (called with mutex held)
    void advance_to_next_char();

    // Release current key (and SHIFT if held)
    void release_current_key();

    KeyboardMatrix& keyboard_;

    // Queue of strings to type (protected by mutex)
    mutable std::mutex mutex_;
    std::queue<QueueEntry> queue_;

    // Current typing state (only accessed from emulator thread)
    State state_ = State::Idle;
    std::string current_text_;
    size_t current_index_ = 0;
    size_t current_cycles_per_key_ = DEFAULT_CYCLES_PER_KEY;
    size_t cycle_count_ = 0;

    // Current key state
    uint8_t current_ik_number_ = 0;
    bool current_needs_shift_ = false;
};

} // namespace beebium
