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

#include "Disc.hpp"
#include "beebium/indicators/IndicatorFilter.hpp"
#include "beebium/indicators/Indicators.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
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

// Physical floppy disc drive emulation with pulse-level access.
//
// Manages:
// - Disc insertion/ejection (including safe eject with quiescence)
// - Head track position (0-79)
// - Motor on/off state with quiescence tracking
// - Pulse-level read/write at current head position
// - Side selection for double-sided discs
//
// Disc controllers (WD1770, 8271, etc.) read individual pulse words from the
// drive as it rotates, just like real hardware reads flux transitions.
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

    // Constructor with indicator integration
    DiscDrive(Indicators& indicators, const std::string& indicator_name,
                    const std::string& label, const std::string& color = "568nm")
        : indicators_(&indicators) {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", label);
        metadata.emplace("color", color);
        metadata.emplace("shape", "rectangular");
        activity_led_id_ = indicators_->register_indicator(
            indicator_name, std::make_unique<PassthroughFilter>(), std::move(metadata));
    }

    ~DiscDrive() = default;

    DiscDrive(const DiscDrive&) = delete;
    DiscDrive& operator=(const DiscDrive&) = delete;
    DiscDrive(DiscDrive&&) = default;
    DiscDrive& operator=(DiscDrive&&) = default;

    // =========================================================================
    // State
    // =========================================================================

    DriveState state() const { return state_; }
    bool has_disc() const { return state_ != DriveState::Empty; }

    // =========================================================================
    // Disc Insertion
    // =========================================================================

    // Insert a disc into the drive.
    // Only valid when state is Empty.
    // Transitions: Empty -> Loaded
    void insert(std::unique_ptr<Disc> disc) {
        disc_ = std::move(disc);
        if (disc_) {
            state_ = DriveState::Loaded;
            source_url_.clear();
            head_position_ = 0;
            pulse_sub_position_ = 0;
        }
    }

    // Insert a disc and record its source URL
    void insert(std::unique_ptr<Disc> disc, const std::string& url) {
        insert(std::move(disc));
        source_url_ = url;
    }

    // =========================================================================
    // Disc Ejection
    // =========================================================================

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
    std::unique_ptr<Disc> tick_eject() {
        if (state_ != DriveState::Ejecting) {
            return nullptr;
        }

        auto now = clock::now();
        auto elapsed = now - eject_requested_at_;

        // Check for force timeout
        if (elapsed >= pending_eject_.force_after) {
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
    std::unique_ptr<Disc> eject_immediate() {
        if (state_ == DriveState::Empty) {
            return nullptr;
        }
        was_forced_eject_ = true;
        return complete_eject();
    }

    // Legacy API - delegates to eject_immediate()
    std::unique_ptr<Disc> eject() { return eject_immediate(); }

    // Was the last ejection forced (timeout or immediate)?
    bool was_forced_eject() const { return was_forced_eject_; }

    // =========================================================================
    // Disc Access
    // =========================================================================

    Disc* disc() const { return disc_.get(); }
    const std::string& source_url() const { return source_url_; }

    // =========================================================================
    // Head Positioning
    // =========================================================================

    void step_in() {
        if (current_track_ < MAX_TRACK) ++current_track_;
    }

    void step_out() {
        if (current_track_ > 0) --current_track_;
    }

    void seek(uint8_t track) {
        current_track_ = (track <= MAX_TRACK) ? track : MAX_TRACK;
    }

    uint8_t current_track() const { return current_track_; }
    bool at_track_0() const { return current_track_ == 0; }

    // =========================================================================
    // Side Selection
    // =========================================================================

    void set_side(uint8_t side) { selected_side_ = side; }
    uint8_t selected_side() const { return selected_side_; }

    // =========================================================================
    // Motor Control
    // =========================================================================

    void spin_up() { set_motor(true); }
    void spin_down() { set_motor(false); }

    void set_motor(bool on) {
        if (motor_on_ != on) {
            if (motor_on_ && !on) {
                motor_off_since_ = clock::now();
            }
            motor_on_ = on;
            if (indicators_) {
                indicators_->set(activity_led_id_, on ? 255 : 0);
            }
        }
    }

    bool motor_on() const { return motor_on_; }

    // Check if motor has been off for at least the specified duration
    bool is_quiescent(std::chrono::milliseconds min_off) const {
        if (motor_on_) return false;
        return (clock::now() - motor_off_since_) >= min_off;
    }

    // Activity LED indicator ID (valid only if constructed with Indicators)
    uint16_t activity_led_id() const { return activity_led_id_; }

    // =========================================================================
    // Pulse-Level Access
    // =========================================================================

    // Read the pulse word at the current head position.
    // If no disc is inserted, returns quasi-random pulses.
    uint32_t read_pulses() const {
        if (!disc_) {
            return quasi_random_pulses();
        }
        bool upper = (selected_side_ == 1);
        return disc_->read_pulses(upper, current_track_, head_position_);
    }

    // Write a pulse word at the current head position.
    void write_pulses(uint32_t pulses) {
        if (!disc_ || disc_->is_write_protected()) return;
        bool upper = (selected_side_ == 1);
        disc_->write_pulses(upper, current_track_, head_position_, pulses);
    }

    // Advance the head by one full pulse word (32 bits = one FM byte, 64us).
    // Returns true if the index pulse occurred (head wrapped around to start of track).
    bool advance_head() {
        ++head_position_;
        pulse_sub_position_ = 0;
        return check_index_wrap();
    }

    // Advance the head by half a pulse word (16 bits = one MFM byte, 32us).
    // Returns true if the index pulse occurred.
    bool advance_head_half() {
        if (pulse_sub_position_ == 0) {
            pulse_sub_position_ = 16;
            return false;
        } else {
            pulse_sub_position_ = 0;
            ++head_position_;
            return check_index_wrap();
        }
    }

    // Current head position in pulse-word units.
    uint32_t head_position() const { return head_position_; }

    // Sub-position within pulse word: 0 = upper half, 16 = lower half (for MFM).
    uint32_t pulse_sub_position() const { return pulse_sub_position_; }

    // Track length for current track (in pulse-word units).
    uint32_t track_length() const {
        if (!disc_) return ibm_disc_format::k_ibm_disc_bytes_per_track;
        bool upper = (selected_side_ == 1);
        return disc_->track(upper, current_track_).length();
    }

    // Whether the head is at the index position (start of track).
    bool is_index_pulse() const {
        return head_position_ == 0 && pulse_sub_position_ == 0;
    }

    // Flush any dirty tracks on the current disc.
    void flush_if_dirty() {
        if (disc_) disc_->flush_dirty_tracks();
    }

    // =========================================================================
    // Write Protection
    // =========================================================================

    bool is_write_protected() const {
        if (!disc_) return false;
        return disc_->is_write_protected();
    }

private:
    std::unique_ptr<Disc> complete_eject() {
        if (disc_) {
            disc_->flush_dirty_tracks();
        }
        state_ = DriveState::Empty;
        source_url_.clear();
        head_position_ = 0;
        pulse_sub_position_ = 0;
        return std::move(disc_);
    }

    bool check_index_wrap() {
        uint32_t len = track_length();
        if (head_position_ >= len) {
            head_position_ = 0;
            // Flush dirty tracks on wraparound (matching beebjit behaviour)
            if (disc_) {
                disc_->flush_track(selected_side_ == 1, current_track_);
            }
            return true;  // Index pulse
        }
        return false;
    }

    // Generate quasi-random pulses for unformatted/empty disc regions.
    uint32_t quasi_random_pulses() const {
        uint32_t x = head_position_ ^ (current_track_ * 7919) ^ (selected_side_ * 65537);
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }

    // Core state
    DriveState state_ = DriveState::Empty;
    std::unique_ptr<Disc> disc_;
    std::string source_url_;

    // Head and motor
    uint8_t current_track_ = 0;
    uint8_t selected_side_ = 0;
    bool motor_on_ = false;
    clock::time_point motor_off_since_ = clock::now();

    // Head position within current track
    uint32_t head_position_ = 0;
    uint32_t pulse_sub_position_ = 0;  // 0 or 16

    // Safe eject state
    EjectOptions pending_eject_;
    clock::time_point eject_requested_at_;
    bool was_forced_eject_ = false;

    // Indicator integration (optional)
    Indicators* indicators_ = nullptr;
    uint16_t activity_led_id_ = 0;
};

} // namespace beebium
