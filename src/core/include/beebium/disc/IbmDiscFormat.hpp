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

#include <cstdint>

namespace beebium {
namespace ibm_disc_format {

// =============================================================================
// Constants
// =============================================================================

// Track geometry
inline constexpr uint32_t k_ibm_disc_bytes_per_track = 3125;
inline constexpr uint32_t k_ibm_disc_tracks_per_disc = 84;
inline constexpr uint32_t k_max_pulse_positions_per_track = 3328;

// FM address mark patterns
inline constexpr uint8_t k_mark_clock_pattern = 0xC7;
inline constexpr uint8_t k_id_mark_data_pattern = 0xFE;
inline constexpr uint8_t k_data_mark_data_pattern = 0xFB;
inline constexpr uint8_t k_deleted_data_mark_data_pattern = 0xF8;

// MFM sync words (special encoding with missing clock transitions)
inline constexpr uint16_t k_mfm_a1_sync = 0x4489;
inline constexpr uint16_t k_mfm_c2_sync = 0x5224;

// Standard gap sizes for 10-sector FM format (DFS)
inline constexpr uint32_t k_std_sync_00s = 6;
inline constexpr uint32_t k_std_gap1_FFs = 16;
inline constexpr uint32_t k_std_gap2_FFs = 11;
inline constexpr uint32_t k_std_10_sector_gap3_FFs = 21;

// =============================================================================
// CRC-CCITT (polynomial x^16 + x^12 + x^5 + 1)
// =============================================================================

// Initialise CRC. FM starts at 0xFFFF; MFM starts at 0xCDB4 (after 3x A1 sync).
constexpr uint16_t crc_init(bool is_mfm) {
    return is_mfm ? 0xCDB4 : 0xFFFF;
}

// Add one byte to the running CRC.
constexpr uint16_t crc_add_byte(uint16_t crc, uint8_t byte) {
    for (uint32_t i = 0; i < 8; ++i) {
        int bit = (byte & 0x80);
        int bit_test = ((crc & 0x8000) ^ (bit << 8));
        crc <<= 1;
        if (bit_test) {
            crc ^= 0x1021;
        }
        byte <<= 1;
    }
    return crc;
}

// =============================================================================
// FM Encoding/Decoding
//
// FM (Frequency Modulation) uses a 4us bit cell. Each data bit is preceded by
// a clock bit. One byte of data + clock produces 32 bits of 2us-resolution
// pulse data (one uint32_t).
//
// Within each 4-bit group (MSB first): bit 2 = clock, bit 0 = data.
// =============================================================================

// Encode one FM byte (clock + data) into a 32-bit pulse word.
constexpr uint32_t fm_to_2us_pulses(uint8_t clocks, uint8_t data) {
    uint32_t ret = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        ret <<= 4;
        if (clocks & 0x80) {
            ret |= 0x04;
        }
        if (data & 0x80) {
            ret |= 0x01;
        }
        clocks <<= 1;
        data <<= 1;
    }
    return ret;
}

// Result of decoding FM pulses.
struct FmDecodeResult {
    uint8_t clocks;
    uint8_t data;
    bool is_iffy_pulse;
};

// Decode a 32-bit pulse word back to FM clock + data bytes.
// Flags "iffy" pulses where bits appear in unexpected positions.
//
// The encoder places clock at bit 2 (0x04) and data at bit 0 (0x01) within
// each 4-bit nibble. The decoder reads from these same positions.
// "Iffy" pulses have energy at the unused positions (bits 3 and 1).
//
// Note: beebjit's decode function reads from different bit positions (3 and 1)
// because its FDC has its own internal pulse decoder. We provide a true inverse
// of the encoder here, plus a separate beebjit-compatible decode for use in
// FDC emulation where the phase offset matters.
constexpr FmDecodeResult fm_from_2us_pulses(uint32_t pulses) {
    uint8_t clocks = 0;
    uint8_t data = 0;
    bool is_iffy_pulse = false;

    // Encoder places: clock at bit 2 (mask 0x04), data at bit 0 (mask 0x01)
    // For MSB nibble of uint32_t: clock at bit 30 (0x40000000), data at bit 28 (0x10000000)
    // Iffy: bits 3 and 1 (0xA0000000) -- energy at unexpected positions
    for (uint32_t i = 0; i < 8; ++i) {
        clocks <<= 1;
        data <<= 1;
        if (pulses & 0x40000000) {
            clocks |= 1;
        }
        if (pulses & 0x10000000) {
            data |= 1;
        }
        if (pulses & 0xA0000000) {
            is_iffy_pulse = true;
        }
        pulses <<= 4;
    }

    return {clocks, data, is_iffy_pulse};
}

// =============================================================================
// MFM Encoding/Decoding
//
// MFM (Modified Frequency Modulation) uses a 2us bit cell. Clock bits are
// inserted only between consecutive zero data bits. One byte of data produces
// 16 bits of 2us-resolution pulse data (one uint16_t).
//
// Within each 2-bit group: bit 1 = clock, bit 0 = data.
// Clock rule: set clock if both current and previous data bits are 0.
// =============================================================================

// Encode one MFM byte into 16-bit pulse data. Updates last_mfm_bit state.
constexpr uint16_t mfm_to_2us_pulses(bool& last_mfm_bit, uint8_t byte) {
    uint16_t pulses = 0;
    bool last_bit = last_mfm_bit;

    for (uint32_t i = 0; i < 8; ++i) {
        bool bit = (byte & 0x80) != 0;
        pulses <<= 2;
        byte <<= 1;
        if (bit) {
            pulses |= 0x01;
        } else if (!last_bit) {
            pulses |= 0x02;
        }
        last_bit = bit;
    }

    last_mfm_bit = last_bit;
    return pulses;
}

// Decode 16-bit MFM pulse data back to a data byte.
constexpr uint8_t mfm_from_2us_pulses(uint16_t pulses) {
    uint8_t byte = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        byte <<= 1;
        if ((pulses & 0xC000) == 0x4000) {
            byte |= 1;
        }
        pulses <<= 2;
    }
    return byte;
}

} // namespace ibm_disc_format
} // namespace beebium
