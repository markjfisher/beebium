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

#include "DiscTrack.hpp"
#include "IbmDiscFormat.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace beebium {

// A floppy disc surface with two sides of pulse-level track data.
//
// This is the central disc object in the pulse-level subsystem. All disc image
// formats are converted to this common representation on load. The FDC reads
// pulses from tracks via the DiscDrive, which delegates to this object.
//
// Write-back is handled by a callback set by the format handler that loaded the
// disc. When dirty tracks are flushed, the callback extracts sector data from
// pulse tracks and writes it back to the original file format.
class Disc {
public:
    // Callback invoked for each dirty track during flush.
    // Parameters: disc reference, upper_side flag, track number.
    using WriteTrackCallback = std::function<void(Disc& disc,
                                                   bool upper_side,
                                                   uint32_t track_number)>;

    Disc() = default;

    // =========================================================================
    // Metadata
    // =========================================================================

    const std::string& name() const { return name_; }
    void set_name(const std::string& name) { name_ = name; }

    bool is_write_protected() const { return write_protected_; }
    void set_write_protected(bool write_protected) { write_protected_ = write_protected; }

    bool is_double_sided() const { return double_sided_; }
    void set_double_sided(bool double_sided) { double_sided_ = double_sided; }

    const std::string& format_name() const { return format_name_; }
    void set_format_name(const std::string& format_name) { format_name_ = format_name; }

    // =========================================================================
    // Source File
    // =========================================================================

    const std::filesystem::path& source_filepath() const { return source_filepath_; }
    void set_source_filepath(const std::filesystem::path& filepath) { source_filepath_ = filepath; }

    // =========================================================================
    // Track Access
    // =========================================================================

    DiscTrack& track(bool upper_side, uint32_t track_number) {
        return upper_side ? upper_side_[track_number] : lower_side_[track_number];
    }

    const DiscTrack& track(bool upper_side, uint32_t track_number) const {
        return upper_side ? upper_side_[track_number] : lower_side_[track_number];
    }

    // Pulse-level read at specific position on a track.
    uint32_t read_pulses(bool upper_side, uint32_t track_number, uint32_t position) const {
        return track(upper_side, track_number).read_pulses(position);
    }

    // Pulse-level write at specific position on a track.
    void write_pulses(bool upper_side, uint32_t track_number,
                      uint32_t position, uint32_t pulses) {
        track(upper_side, track_number).write_pulses(position, pulses);
    }

    // =========================================================================
    // Dirty Tracking and Write-Back
    // =========================================================================

    void set_write_track_callback(WriteTrackCallback callback) {
        write_track_callback_ = std::move(callback);
    }

    // Check if any track on either side is dirty.
    bool has_dirty_tracks() const {
        for (const auto& t : lower_side_) {
            if (t.is_dirty()) return true;
        }
        for (const auto& t : upper_side_) {
            if (t.is_dirty()) return true;
        }
        return false;
    }

    // Flush all dirty tracks via the write-back callback, then clear dirty flags.
    void flush_dirty_tracks() {
        if (!write_track_callback_) return;

        for (uint32_t i = 0; i < lower_side_.size(); ++i) {
            if (lower_side_[i].is_dirty()) {
                write_track_callback_(*this, false, i);
                lower_side_[i].set_dirty(false);
            }
        }
        for (uint32_t i = 0; i < upper_side_.size(); ++i) {
            if (upper_side_[i].is_dirty()) {
                write_track_callback_(*this, true, i);
                upper_side_[i].set_dirty(false);
            }
        }
    }

    // Flush a single dirty track.
    void flush_track(bool upper_side, uint32_t track_number) {
        auto& t = track(upper_side, track_number);
        if (t.is_dirty() && write_track_callback_) {
            write_track_callback_(*this, upper_side, track_number);
            t.set_dirty(false);
        }
    }

    // =========================================================================
    // Format Metadata (opaque blob for format handlers)
    // =========================================================================

    std::span<uint8_t> format_metadata() { return format_metadata_; }
    std::span<const uint8_t> format_metadata() const { return format_metadata_; }
    void allocate_format_metadata(size_t num_bytes) { format_metadata_.resize(num_bytes, 0); }

private:
    std::string name_;
    std::string format_name_;
    bool write_protected_ = false;
    bool double_sided_ = false;

    std::array<DiscTrack, ibm_disc_format::k_ibm_disc_tracks_per_disc> lower_side_{};
    std::array<DiscTrack, ibm_disc_format::k_ibm_disc_tracks_per_disc> upper_side_{};

    WriteTrackCallback write_track_callback_;
    std::vector<uint8_t> format_metadata_;
    std::filesystem::path source_filepath_;
};

} // namespace beebium
