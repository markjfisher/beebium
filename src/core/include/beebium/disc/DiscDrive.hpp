// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "DiscImage.hpp"
#include <cstdint>
#include <memory>
#include <span>

namespace beebium {

// Physical floppy disc drive emulation.
//
// Manages:
// - Disc insertion/ejection
// - Head track position (0-79)
// - Motor on/off state
// - Sector read/write at current track
//
// The drive delegates actual sector I/O to the inserted DiscImage.
// The disc controller (WD1770 or 8271) sends commands to the drive.
class DiscDrive {
public:
    static constexpr uint8_t MAX_TRACK = 79;

    DiscDrive() = default;
    ~DiscDrive() = default;

    // Non-copyable, movable
    DiscDrive(const DiscDrive&) = delete;
    DiscDrive& operator=(const DiscDrive&) = delete;
    DiscDrive(DiscDrive&&) = default;
    DiscDrive& operator=(DiscDrive&&) = default;

    // Disc management
    void insert(std::unique_ptr<DiscImage> disc) { disc_ = std::move(disc); }
    std::unique_ptr<DiscImage> eject() { return std::move(disc_); }
    bool has_disc() const { return disc_ != nullptr; }
    DiscImage* disc() const { return disc_.get(); }

    // Head positioning
    void step_in() {
        if (current_track_ < MAX_TRACK) {
            ++current_track_;
        }
    }

    void step_out() {
        if (current_track_ > 0) {
            --current_track_;
        }
    }

    void seek(uint8_t track) {
        current_track_ = (track <= MAX_TRACK) ? track : MAX_TRACK;
    }

    uint8_t current_track() const { return current_track_; }
    bool at_track_0() const { return current_track_ == 0; }

    // Motor control
    void set_motor(bool on) { motor_on_ = on; }
    bool motor_on() const { return motor_on_; }

    // Sector access at current track
    // Side is 0 or 1, sector is the sector number on the track
    bool read_sector(uint8_t side, uint8_t sector, std::span<uint8_t> buffer) {
        if (!disc_) {
            return false;
        }
        return disc_->read_sector(side, current_track_, sector, buffer);
    }

    bool write_sector(uint8_t side, uint8_t sector, std::span<const uint8_t> buffer) {
        if (!disc_) {
            return false;
        }
        return disc_->write_sector(side, current_track_, sector, buffer);
    }

    // Status
    bool is_write_protected() const {
        if (!disc_) {
            return false;
        }
        return disc_->is_write_protected();
    }

private:
    std::unique_ptr<DiscImage> disc_;
    uint8_t current_track_ = 0;
    bool motor_on_ = false;
};

} // namespace beebium
