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

#include "IbmDiscFormat.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>

namespace beebium {

// A single track of pulse data on one side of a floppy disc.
//
// Stores an array of uint32_t pulse words at 2us resolution. Each pulse word
// represents one FM byte position (32 bits = 64us at 250kbps) or two MFM byte
// positions (16 bits each = 32us per byte).
//
// Standard track length is 3125 pulse positions (one revolution at 300 RPM).
// The buffer has headroom to 3328 for non-standard or long tracks.
class DiscTrack {
public:
    DiscTrack() = default;

    // Track length in pulse-word positions.
    uint32_t length() const { return length_; }

    // Set the number of valid pulse positions. Clamped to maximum.
    void set_length(uint32_t length) {
        length_ = std::min(length, static_cast<uint32_t>(pulses_.size()));
    }

    // Read the pulse word at the given position.
    uint32_t read_pulses(uint32_t position) const {
        assert(position < pulses_.size());
        return pulses_[position];
    }

    // Write a pulse word at the given position. Marks the track dirty.
    void write_pulses(uint32_t position, uint32_t pulses) {
        assert(position < pulses_.size());
        pulses_[position] = pulses;
        dirty_ = true;
    }

    // Direct access to the pulse buffer.
    std::span<uint32_t> pulses() { return pulses_; }
    std::span<const uint32_t> pulses() const { return pulses_; }

    // Dirty tracking for write-back.
    bool is_dirty() const { return dirty_; }
    void set_dirty(bool dirty) { dirty_ = dirty; }

    // Reset all pulses to zero and clear dirty flag. Length is preserved.
    void clear() {
        pulses_.fill(0);
        dirty_ = false;
    }

private:
    std::array<uint32_t, ibm_disc_format::k_max_pulse_positions_per_track> pulses_{};
    uint32_t length_ = ibm_disc_format::k_ibm_disc_bytes_per_track;
    bool dirty_ = false;
};

} // namespace beebium
