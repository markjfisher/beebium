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
#include <vector>

namespace beebium {

// Information about a sector found on a track.
struct SectorHeader {
    bool is_fm = true;           // true=FM, false=MFM
    uint8_t track = 0;           // Track number from ID field
    uint8_t side = 0;            // Side number from ID field
    uint8_t sector = 0;          // Sector number from ID field
    uint8_t size_code = 0;       // 0=128, 1=256, 2=512, 3=1024

    uint16_t header_crc = 0;
    bool has_header_crc_error = false;

    // Data field position. For FM: pulse-word index. For MFM: half-word index.
    uint32_t data_pulse_position = 0;
    uint32_t data_sub_position = 0;  // 0 = upper half, 16 = lower half (MFM only)
    bool is_deleted = false;
    bool has_data_field = false;
    uint16_t data_crc = 0;
    bool has_data_crc_error = false;

    uint32_t data_byte_length() const { return 128u << size_code; }
};

// Decoder for extracting sector information from pulse-level track data.
//
// Scans a DiscTrack for both FM and MFM sectors. FM scanning looks for
// special clock patterns (0xC7). MFM scanning looks for the A1 sync word
// (0x4489) followed by an address mark byte.
class TrackDecoder {
public:
    explicit TrackDecoder(const DiscTrack& track)
        : track_(track) {}

    // Scan the entire track for FM sectors.
    std::vector<SectorHeader> find_fm_sectors() const {
        std::vector<SectorHeader> sectors;
        uint32_t pos = 0;
        uint32_t length = track_.length();

        while (pos < length) {
            auto [clocks, data, iffy] = ibm_disc_format::fm_from_2us_pulses(
                track_.read_pulses(pos));

            if (clocks == ibm_disc_format::k_mark_clock_pattern &&
                data == ibm_disc_format::k_id_mark_data_pattern) {

                SectorHeader header;
                header.is_fm = true;
                if (pos + 7 > length) break;

                uint16_t crc = ibm_disc_format::crc_init(false);
                crc = ibm_disc_format::crc_add_byte(crc, data);

                header.track = read_fm_at(pos + 1);
                crc = ibm_disc_format::crc_add_byte(crc, header.track);
                header.side = read_fm_at(pos + 2);
                crc = ibm_disc_format::crc_add_byte(crc, header.side);
                header.sector = read_fm_at(pos + 3);
                crc = ibm_disc_format::crc_add_byte(crc, header.sector);
                header.size_code = read_fm_at(pos + 4);
                crc = ibm_disc_format::crc_add_byte(crc, header.size_code);

                uint8_t crc_hi = read_fm_at(pos + 5);
                uint8_t crc_lo = read_fm_at(pos + 6);
                header.header_crc = (static_cast<uint16_t>(crc_hi) << 8) | crc_lo;
                header.has_header_crc_error = (crc != header.header_crc);

                // Search for data mark
                uint32_t search_limit = std::min(pos + 57, length);
                bool found = false;
                for (uint32_t dp = pos + 7; dp < search_limit; ++dp) {
                    auto [dc, dd, di] = ibm_disc_format::fm_from_2us_pulses(
                        track_.read_pulses(dp));
                    if (dc == ibm_disc_format::k_mark_clock_pattern) {
                        if (dd == ibm_disc_format::k_data_mark_data_pattern ||
                            dd == ibm_disc_format::k_deleted_data_mark_data_pattern) {
                            header.is_deleted = (dd == ibm_disc_format::k_deleted_data_mark_data_pattern);
                            header.has_data_field = true;
                            header.data_pulse_position = dp + 1;
                            found = true;
                            verify_fm_data_crc(header);
                            break;
                        }
                    }
                }

                sectors.push_back(header);
                pos = found ? header.data_pulse_position + header.data_byte_length() + 2 : pos + 7;
            } else {
                ++pos;
            }
        }
        return sectors;
    }

    // Scan the entire track for MFM sectors.
    std::vector<SectorHeader> find_mfm_sectors() const {
        std::vector<SectorHeader> sectors;
        uint32_t length = track_.length();

        // Scan in half-word units (each pulse word has upper and lower 16-bit halves)
        uint32_t total_halves = length * 2;
        uint32_t half = 0;

        while (half < total_halves) {
            uint16_t hw = read_mfm_half(half);

            // Look for A1 sync word (0x4489)
            if (hw == ibm_disc_format::k_mfm_a1_sync) {
                // Need at least 2 more A1 syncs + mark byte + 4 ID bytes + 2 CRC = 9 more half-words
                if (half + 9 >= total_halves) break;

                // Check for 3xA1 pattern (we found the first, check next two)
                uint16_t hw2 = read_mfm_half(half + 1);
                uint16_t hw3 = read_mfm_half(half + 2);
                if (hw2 != ibm_disc_format::k_mfm_a1_sync ||
                    hw3 != ibm_disc_format::k_mfm_a1_sync) {
                    ++half;
                    continue;
                }

                // Read the mark byte (after 3xA1)
                uint8_t mark = ibm_disc_format::mfm_from_2us_pulses(read_mfm_half(half + 3));

                if (mark == ibm_disc_format::k_id_mark_data_pattern) {
                    // ID field found
                    SectorHeader header;
                    header.is_fm = false;

                    uint16_t crc = ibm_disc_format::crc_init(true);  // MFM CRC (post 3xA1)
                    crc = ibm_disc_format::crc_add_byte(crc, mark);

                    header.track = read_mfm_byte(half + 4);
                    crc = ibm_disc_format::crc_add_byte(crc, header.track);
                    header.side = read_mfm_byte(half + 5);
                    crc = ibm_disc_format::crc_add_byte(crc, header.side);
                    header.sector = read_mfm_byte(half + 6);
                    crc = ibm_disc_format::crc_add_byte(crc, header.sector);
                    header.size_code = read_mfm_byte(half + 7);
                    crc = ibm_disc_format::crc_add_byte(crc, header.size_code);

                    uint8_t crc_hi = read_mfm_byte(half + 8);
                    uint8_t crc_lo = read_mfm_byte(half + 9);
                    header.header_crc = (static_cast<uint16_t>(crc_hi) << 8) | crc_lo;
                    header.has_header_crc_error = (crc != header.header_crc);

                    // Search for data mark (3xA1 + FB/F8) after GAP2
                    uint32_t search_start = half + 10;
                    uint32_t search_limit = std::min(search_start + 100, total_halves - 3);
                    bool found = false;

                    for (uint32_t dp = search_start; dp < search_limit; ++dp) {
                        if (read_mfm_half(dp) == ibm_disc_format::k_mfm_a1_sync &&
                            read_mfm_half(dp + 1) == ibm_disc_format::k_mfm_a1_sync &&
                            read_mfm_half(dp + 2) == ibm_disc_format::k_mfm_a1_sync) {
                            uint8_t dm = read_mfm_byte(dp + 3);
                            if (dm == ibm_disc_format::k_data_mark_data_pattern ||
                                dm == ibm_disc_format::k_deleted_data_mark_data_pattern) {
                                header.is_deleted = (dm == ibm_disc_format::k_deleted_data_mark_data_pattern);
                                header.has_data_field = true;
                                // Data starts after 3xA1 + mark byte = dp + 4
                                header.data_pulse_position = (dp + 4) / 2;
                                header.data_sub_position = ((dp + 4) % 2) * 16;
                                found = true;
                                verify_mfm_data_crc(header, dm, dp + 4);
                                break;
                            }
                        }
                    }

                    sectors.push_back(header);
                    if (found) {
                        // Skip past data + CRC
                        uint32_t data_halves = header.data_byte_length() + 2;
                        half = (header.data_pulse_position * 2 +
                                (header.data_sub_position ? 1 : 0)) + data_halves;
                    } else {
                        half += 10;
                    }
                } else {
                    ++half;
                }
            } else {
                ++half;
            }
        }
        return sectors;
    }

    // Scan for both FM and MFM sectors (tries FM first, then MFM if none found).
    std::vector<SectorHeader> find_sectors() const {
        auto sectors = find_fm_sectors();
        if (!sectors.empty()) return sectors;
        return find_mfm_sectors();
    }

    // Read sector data bytes from a previously found sector header.
    uint32_t read_sector_data(const SectorHeader& header,
                               std::span<uint8_t> buffer) const {
        if (!header.has_data_field) return 0;

        uint32_t data_len = std::min(
            static_cast<uint32_t>(buffer.size()),
            header.data_byte_length());

        if (header.is_fm) {
            for (uint32_t i = 0; i < data_len; ++i) {
                uint32_t p = header.data_pulse_position + i;
                if (p >= track_.length()) { data_len = i; break; }
                buffer[i] = read_fm_at(p);
            }
        } else {
            // MFM: data_pulse_position and data_sub_position define the half-word start
            uint32_t start_half = header.data_pulse_position * 2 +
                                  (header.data_sub_position ? 1 : 0);
            for (uint32_t i = 0; i < data_len; ++i) {
                uint32_t h = start_half + i;
                if (h / 2 >= track_.length()) { data_len = i; break; }
                buffer[i] = read_mfm_byte(h);
            }
        }

        return data_len;
    }

private:
    uint8_t read_fm_at(uint32_t position) const {
        auto [c, d, i] = ibm_disc_format::fm_from_2us_pulses(
            track_.read_pulses(position));
        return d;
    }

    // Read a 16-bit MFM half-word. half_index 0 = upper 16 bits of word 0,
    // half_index 1 = lower 16 bits of word 0, half_index 2 = upper of word 1, etc.
    uint16_t read_mfm_half(uint32_t half_index) const {
        uint32_t word_index = half_index / 2;
        if (word_index >= track_.length()) return 0;
        uint32_t pulse = track_.read_pulses(word_index);
        if (half_index & 1) {
            return static_cast<uint16_t>(pulse & 0xFFFF);
        } else {
            return static_cast<uint16_t>(pulse >> 16);
        }
    }

    // Read and decode an MFM byte from a half-word index.
    uint8_t read_mfm_byte(uint32_t half_index) const {
        return ibm_disc_format::mfm_from_2us_pulses(read_mfm_half(half_index));
    }

    void verify_fm_data_crc(SectorHeader& header) const {
        uint32_t data_len = header.data_byte_length();
        uint32_t data_end = header.data_pulse_position + data_len + 2;
        if (data_end > track_.length()) return;

        uint16_t crc = ibm_disc_format::crc_init(false);
        crc = ibm_disc_format::crc_add_byte(crc,
            header.is_deleted ? ibm_disc_format::k_deleted_data_mark_data_pattern
                              : ibm_disc_format::k_data_mark_data_pattern);
        for (uint32_t i = 0; i < data_len; ++i) {
            crc = ibm_disc_format::crc_add_byte(crc, read_fm_at(header.data_pulse_position + i));
        }
        uint8_t hi = read_fm_at(header.data_pulse_position + data_len);
        uint8_t lo = read_fm_at(header.data_pulse_position + data_len + 1);
        header.data_crc = (static_cast<uint16_t>(hi) << 8) | lo;
        header.has_data_crc_error = (crc != header.data_crc);
    }

    void verify_mfm_data_crc(SectorHeader& header, uint8_t mark_byte, uint32_t data_start_half) const {
        uint32_t data_len = header.data_byte_length();
        uint32_t data_end_half = data_start_half + data_len + 2;
        if (data_end_half / 2 >= track_.length()) return;

        uint16_t crc = ibm_disc_format::crc_init(true);
        crc = ibm_disc_format::crc_add_byte(crc, mark_byte);
        for (uint32_t i = 0; i < data_len; ++i) {
            crc = ibm_disc_format::crc_add_byte(crc, read_mfm_byte(data_start_half + i));
        }
        uint8_t hi = read_mfm_byte(data_start_half + data_len);
        uint8_t lo = read_mfm_byte(data_start_half + data_len + 1);
        header.data_crc = (static_cast<uint16_t>(hi) << 8) | lo;
        header.has_data_crc_error = (crc != header.data_crc);
    }

    const DiscTrack& track_;
};

} // namespace beebium
