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

#include <cstdint>
#include <span>

namespace beebium {

// Stateful builder for constructing pulse-level tracks from byte data.
//
// Maintains build position within the track, MFM encoding state (last bit),
// and a running CRC. Provides FM and MFM byte-level append operations that
// convert data to 2us-resolution pulse words.
//
// FM bytes occupy one full pulse position (32 bits = 64us).
// MFM bytes occupy half a pulse position (16 bits = 32us). Two MFM bytes
// are packed into one uint32_t pulse word (upper 16 then lower 16 bits).
class TrackBuilder {
public:
    explicit TrackBuilder(DiscTrack& track)
        : track_(track) {}

    // =========================================================================
    // Position and State
    // =========================================================================

    // Current build position (in pulse-word units).
    uint32_t position() const { return build_index_; }

    // Reset builder to start of track.
    void reset() {
        build_index_ = 0;
        build_sub_index_ = 0;
        last_mfm_bit_ = false;
        crc_ = 0xFFFF;
    }

    // Set track length to current position (call after building is complete).
    void finalize() {
        // If we have a partial MFM word, flush it
        if (build_sub_index_ != 0) {
            ++build_index_;
            build_sub_index_ = 0;
        }
        track_.set_length(build_index_);
    }

    // Check if the builder has reached the track capacity.
    bool is_full() const {
        return build_index_ >= ibm_disc_format::k_max_pulse_positions_per_track;
    }

    // =========================================================================
    // CRC Management
    // =========================================================================

    // Reset the running CRC. FM uses 0xFFFF; MFM uses 0xCDB4 (post-3xA1).
    void reset_crc(bool is_mfm) {
        crc_ = ibm_disc_format::crc_init(is_mfm);
    }

    // Current CRC value.
    uint16_t current_crc() const { return crc_; }

    // Append two FM-encoded CRC bytes (high byte first).
    void append_crc_fm() {
        uint16_t crc = crc_;
        // CRC bytes are NOT included in the CRC calculation
        uint8_t hi = static_cast<uint8_t>(crc >> 8);
        uint8_t lo = static_cast<uint8_t>(crc & 0xFF);
        append_fm_byte_no_crc(hi);
        append_fm_byte_no_crc(lo);
    }

    // Append two MFM-encoded CRC bytes (high byte first).
    void append_crc_mfm() {
        uint16_t crc = crc_;
        uint8_t hi = static_cast<uint8_t>(crc >> 8);
        uint8_t lo = static_cast<uint8_t>(crc & 0xFF);
        append_mfm_byte_no_crc(hi);
        append_mfm_byte_no_crc(lo);
    }

    // =========================================================================
    // FM Operations
    // =========================================================================

    // Append one FM byte with standard clocks (0xFF). Updates CRC.
    void append_fm_byte(uint8_t data) {
        crc_ = ibm_disc_format::crc_add_byte(crc_, data);
        append_fm_byte_no_crc(data);
    }

    // Append one FM byte with custom clock pattern. Updates CRC.
    void append_fm_data_and_clocks(uint8_t data, uint8_t clocks) {
        crc_ = ibm_disc_format::crc_add_byte(crc_, data);
        write_fm_pulse(clocks, data);
    }

    // Append N copies of an FM byte with standard clocks. Updates CRC.
    void append_fm_bytes(uint8_t data, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            append_fm_byte(data);
        }
    }

    // Append a span of FM bytes with standard clocks. Updates CRC.
    void append_fm_chunk(std::span<const uint8_t> data) {
        for (uint8_t byte : data) {
            append_fm_byte(byte);
        }
    }

    // Fill remaining track positions with an FM byte. Does NOT update CRC.
    void fill_fm_byte(uint8_t data) {
        uint32_t pulses = ibm_disc_format::fm_to_2us_pulses(0xFF, data);
        while (build_index_ < track_.length()) {
            track_.pulses()[build_index_] = pulses;
            ++build_index_;
        }
    }

    // =========================================================================
    // MFM Operations
    // =========================================================================

    // Append one MFM byte. Updates CRC.
    void append_mfm_byte(uint8_t data) {
        crc_ = ibm_disc_format::crc_add_byte(crc_, data);
        append_mfm_byte_no_crc(data);
    }

    // Append N copies of an MFM byte. Updates CRC.
    void append_mfm_bytes(uint8_t data, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            append_mfm_byte(data);
        }
    }

    // Append a span of MFM bytes. Updates CRC.
    void append_mfm_chunk(std::span<const uint8_t> data) {
        for (uint8_t byte : data) {
            append_mfm_byte(byte);
        }
    }

    // Append the special MFM 3xA1 sync pattern with missing clock transitions.
    // Resets CRC to the MFM init value (which accounts for the 3 A1 bytes).
    void append_mfm_3x_a1_sync() {
        // The A1 sync uses a special encoding: 0x4489 instead of the normal
        // MFM encoding of 0xA1. This has a "missing clock" transition that
        // the FDC uses to identify the start of a header or data field.
        for (int i = 0; i < 3; ++i) {
            write_mfm_raw(ibm_disc_format::k_mfm_a1_sync);
            // Update last_mfm_bit_ to match the last bit of 0xA1 (bit 0 = 1)
            last_mfm_bit_ = true;
        }
        // CRC is initialised to 0xCDB4 which equals CRC(0xFFFF, A1, A1, A1)
        crc_ = ibm_disc_format::crc_init(true);
    }

    // Fill remaining track positions with an MFM byte. Does NOT update CRC.
    void fill_mfm_byte(uint8_t data) {
        while (build_index_ < track_.length()) {
            append_mfm_byte_no_crc(data);
        }
    }

private:
    // Write an FM pulse word (one full position).
    void write_fm_pulse(uint8_t clocks, uint8_t data) {
        if (build_index_ >= ibm_disc_format::k_max_pulse_positions_per_track) return;
        track_.pulses()[build_index_] = ibm_disc_format::fm_to_2us_pulses(clocks, data);
        ++build_index_;
    }

    // Append one FM byte without CRC update.
    void append_fm_byte_no_crc(uint8_t data) {
        write_fm_pulse(0xFF, data);
    }

    // Write a raw 16-bit MFM value into the current half-word position.
    void write_mfm_raw(uint16_t pulses) {
        if (build_index_ >= ibm_disc_format::k_max_pulse_positions_per_track) return;

        if (build_sub_index_ == 0) {
            // Upper 16 bits of the pulse word
            track_.pulses()[build_index_] =
                (track_.pulses()[build_index_] & 0x0000FFFF) |
                (static_cast<uint32_t>(pulses) << 16);
            build_sub_index_ = 16;
        } else {
            // Lower 16 bits of the pulse word
            track_.pulses()[build_index_] =
                (track_.pulses()[build_index_] & 0xFFFF0000) |
                static_cast<uint32_t>(pulses);
            build_sub_index_ = 0;
            ++build_index_;
        }
    }

    // Append one MFM byte without CRC update.
    void append_mfm_byte_no_crc(uint8_t data) {
        uint16_t pulses = ibm_disc_format::mfm_to_2us_pulses(last_mfm_bit_, data);
        write_mfm_raw(pulses);
    }

    DiscTrack& track_;
    uint32_t build_index_ = 0;
    uint32_t build_sub_index_ = 0;  // 0 = upper half, 16 = lower half (for MFM)
    bool last_mfm_bit_ = false;
    uint16_t crc_ = 0xFFFF;
};

} // namespace beebium
