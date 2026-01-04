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
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace beebium {

// Unified drive state - single source of truth to avoid race conditions
enum class DriveState {
    Empty,      // No disc in drive
    Loaded,     // Disc present, idle
    Ejecting    // Eject pending - waiting for motor quiescence (auto-forces after timeout)
};

// Options for safe disc ejection
struct EjectOptions {
    // Minimum time motor must be off before ejecting (default: 500ms)
    std::chrono::milliseconds quiescence_duration{500};

    // Force eject after this timeout regardless of motor state (default: 10s)
    std::chrono::milliseconds force_after{10000};
};

// Physical floppy disc drive emulation.
//
// Manages:
// - Disc insertion/ejection (including safe eject with quiescence)
// - Head track position (0-79)
// - Motor on/off state with quiescence tracking
// - Sector read/write at current track
//
// The drive delegates actual sector I/O to the inserted DiscImage.
// The disc controller (WD1770 or 8271) sends commands to the drive.
//
// State transitions:
//   Empty -> Loaded      (via insert())
//   Loaded -> Ejecting   (via request_eject())
//   Ejecting -> Empty    (via tick_eject() when quiescent or timeout)
//   Ejecting -> Loaded   (via cancel_eject())
//   Any -> Empty         (via eject_immediate())
class DiscDrive {
public:
    static constexpr uint8_t MAX_TRACK = 79;

    using clock = std::chrono::steady_clock;

    DiscDrive() = default;
    ~DiscDrive() = default;

    // Non-copyable, movable
    DiscDrive(const DiscDrive&) = delete;
    DiscDrive& operator=(const DiscDrive&) = delete;
    DiscDrive(DiscDrive&&) = default;
    DiscDrive& operator=(DiscDrive&&) = default;

    // --- State query (unified, atomic) ---

    DriveState state() const { return state_; }
    bool has_disc() const { return state_ != DriveState::Empty; }

    // --- Disc insertion ---

    // Insert a disc into the drive.
    // Only valid when state is Empty.
    // Transitions: Empty -> Loaded
    void insert(std::unique_ptr<DiscImage> disc) {
        disc_ = std::move(disc);
        if (disc_) {
            state_ = DriveState::Loaded;
            source_url_.clear();
        }
    }

    // Insert a disc and record its source URL
    void insert(std::unique_ptr<DiscImage> disc, const std::string& url) {
        insert(std::move(disc));
        source_url_ = url;
    }

    // --- Disc ejection ---

    // Request safe ejection - waits for motor quiescence.
    // Returns immediately; actual ejection happens in tick_eject().
    // Returns false if drive is already empty or ejecting.
    // Transitions: Loaded -> Ejecting
    bool request_eject(const EjectOptions& opts = {}) {
        if (state_ != DriveState::Loaded) {
            return false;
        }
        state_ = DriveState::Ejecting;
        pending_eject_ = opts;
        eject_requested_at_ = clock::now();
        return true;
    }

    // Check quiescence and perform ejection if ready.
    // Call this periodically (e.g., every 100ms) from the server loop.
    // Returns the ejected disc if ejection occurred, nullptr otherwise.
    std::unique_ptr<DiscImage> tick_eject() {
        if (state_ != DriveState::Ejecting) {
            return nullptr;
        }

        auto now = clock::now();
        auto elapsed = now - eject_requested_at_;

        // Check for force timeout
        if (elapsed >= pending_eject_.force_after) {
            // Force eject regardless of motor state
            was_forced_eject_ = true;
            return complete_eject();
        }

        // Check for quiescence (motor off long enough)
        if (is_quiescent(pending_eject_.quiescence_duration)) {
            was_forced_eject_ = false;
            return complete_eject();
        }

        return nullptr;
    }

    // Cancel a pending eject request.
    // Transitions: Ejecting -> Loaded
    void cancel_eject() {
        if (state_ == DriveState::Ejecting) {
            state_ = DriveState::Loaded;
        }
    }

    // Immediate eject - bypasses quiescence, ejects now.
    // Transitions: Loaded/Ejecting -> Empty
    std::unique_ptr<DiscImage> eject_immediate() {
        if (state_ == DriveState::Empty) {
            return nullptr;
        }
        was_forced_eject_ = true;
        return complete_eject();
    }

    // Legacy API - delegates to eject_immediate()
    std::unique_ptr<DiscImage> eject() { return eject_immediate(); }

    // Was the last ejection forced (timeout or immediate)?
    bool was_forced_eject() const { return was_forced_eject_; }

    // --- Disc access ---

    DiscImage* disc() const { return disc_.get(); }
    const std::string& source_url() const { return source_url_; }

    // --- Head positioning ---

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

    // --- Motor control ---

    void set_motor(bool on) {
        if (motor_on_ && !on) {
            // Motor turning off - record time for quiescence tracking
            motor_off_since_ = clock::now();
        }
        motor_on_ = on;
    }

    bool motor_on() const { return motor_on_; }

    // Check if motor has been off for at least the specified duration
    bool is_quiescent(std::chrono::milliseconds min_off) const {
        if (motor_on_) {
            return false;
        }
        return (clock::now() - motor_off_since_) >= min_off;
    }

    // --- Sector access at current track ---

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

    // --- Status ---

    bool is_write_protected() const {
        if (!disc_) {
            return false;
        }
        return disc_->is_write_protected();
    }

private:
    std::unique_ptr<DiscImage> complete_eject() {
        if (disc_) {
            disc_->flush();
        }
        state_ = DriveState::Empty;
        source_url_.clear();
        return std::move(disc_);
    }

    // Core state
    DriveState state_ = DriveState::Empty;
    std::unique_ptr<DiscImage> disc_;
    std::string source_url_;

    // Head and motor
    uint8_t current_track_ = 0;
    bool motor_on_ = false;
    clock::time_point motor_off_since_ = clock::now();

    // Safe eject state
    EjectOptions pending_eject_;
    clock::time_point eject_requested_at_;
    bool was_forced_eject_ = false;
};

} // namespace beebium
