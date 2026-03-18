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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace beebium {

// Fixed-size instruction trace entry.  16 bytes, packed for cache efficiency.
struct InstructionTraceEntry {
    uint64_t cycle;     // CPU cycle count
    uint16_t pc;        // Program counter
    uint8_t  a;         // Accumulator
    uint8_t  x;         // X register
    uint8_t  y;         // Y register
    uint8_t  sp;        // Stack pointer (low byte)
    uint8_t  p;         // Processor status
    uint8_t  opcode;    // Opcode byte at PC
};

static_assert(sizeof(InstructionTraceEntry) == 16,
    "InstructionTraceEntry must be exactly 16 bytes");

// Pre-allocated ring buffer for instruction-level tracing.
//
// Zero overhead when disabled.  When enabled, records one entry per
// instruction boundary (M6502_IsAboutToExecute) with a single pointer
// bump -- no allocations, no I/O, no locks in the hot path.
//
// The buffer wraps when full, preserving the most recent entries.
// total_count tracks the total number of entries recorded (including
// those overwritten by wrapping).
class InstructionTrace {
public:
    // Construct with no allocation.  Call resize() before enabling.
    InstructionTrace() = default;

    // Construct with the given capacity (number of entries).
    // Memory is allocated once at construction.
    explicit InstructionTrace(size_t capacity)
        : entries_(capacity)
    {}

    // Resize the buffer.  Clears all recorded entries.
    void resize(size_t capacity) {
        entries_.resize(capacity);
        clear();
    }

    // Enable or disable recording.  Disabled by default.
    // The buffer must have been sized (via constructor or resize()) before enabling.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    // Record an entry.  Inlined for zero-overhead in the hot path.
    void record(uint64_t cycle, uint16_t pc, uint8_t a, uint8_t x,
                uint8_t y, uint8_t sp, uint8_t p, uint8_t opcode) {
        if (!enabled_) return;
        auto& e = entries_[write_pos_];
        e.cycle = cycle;
        e.pc = pc;
        e.a = a;
        e.x = x;
        e.y = y;
        e.sp = sp;
        e.p = p;
        e.opcode = opcode;
        if (++write_pos_ >= entries_.size())
            write_pos_ = 0;
        ++total_count_;
    }

    // Clear all recorded entries.
    void clear() {
        write_pos_ = 0;
        total_count_ = 0;
    }

    // Total entries recorded (including overwritten ones).
    uint64_t total_count() const { return total_count_; }

    // Number of entries currently available (min of total_count and capacity).
    size_t available() const {
        return total_count_ < entries_.size() ? static_cast<size_t>(total_count_) : entries_.size();
    }

    // Capacity (max entries before wrapping).
    size_t capacity() const { return entries_.size(); }

    // Access entry by index into the available range.
    // Index 0 is the oldest available entry; index (available()-1) is the newest.
    const InstructionTraceEntry& operator[](size_t index) const {
        // Start position: if we've wrapped, oldest is at write_pos_
        size_t start = (total_count_ <= entries_.size()) ? 0 : write_pos_;
        return entries_[(start + index) % entries_.size()];
    }

    // Direct access to the underlying buffer for bulk export.
    const std::vector<InstructionTraceEntry>& entries() const { return entries_; }
    size_t write_pos() const { return write_pos_; }

private:
    std::vector<InstructionTraceEntry> entries_;
    size_t write_pos_ = 0;
    uint64_t total_count_ = 0;
    bool enabled_ = false;
};

}  // namespace beebium
