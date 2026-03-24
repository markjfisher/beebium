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

    uint32_t data_pulse_position = 0;  // Pulse position of data field start (after mark)
    bool is_deleted = false;           // Deleted data mark (0xF8 vs 0xFB)
    bool has_data_field = false;       // Whether a data field was found after the ID
    uint16_t data_crc = 0;
    bool has_data_crc_error = false;

    // Number of data bytes (128 << size_code)
    uint32_t data_byte_length() const { return 128u << size_code; }
};

// Decoder for extracting sector information from pulse-level track data.
//
// Scans a DiscTrack for FM address marks (ID marks and data marks),
// extracts sector headers, and reads sector data.
class TrackDecoder {
public:
    explicit TrackDecoder(const DiscTrack& track)
        : track_(track) {}

    // Scan the entire track and return all sectors found.
    std::vector<SectorHeader> find_sectors() const {
        std::vector<SectorHeader> sectors;
        uint32_t pos = 0;
        uint32_t length = track_.length();

        while (pos < length) {
            // Read FM byte at this position
            auto [clocks, data, iffy] = ibm_disc_format::fm_from_2us_pulses(
                track_.read_pulses(pos));

            // Look for ID address mark: clock=0xC7, data=0xFE
            if (clocks == ibm_disc_format::k_mark_clock_pattern &&
                data == ibm_disc_format::k_id_mark_data_pattern) {

                SectorHeader header;
                header.is_fm = true;

                // Need 6 more bytes: track, side, sector, size, CRC_hi, CRC_lo
                if (pos + 7 > length) break;

                // Compute CRC over the ID field
                uint16_t crc = ibm_disc_format::crc_init(false);
                crc = ibm_disc_format::crc_add_byte(crc, data);  // Include the mark byte

                // Read ID field
                auto read_fm_byte = [&](uint32_t p) -> uint8_t {
                    auto [c, d, i] = ibm_disc_format::fm_from_2us_pulses(
                        track_.read_pulses(p));
                    return d;
                };

                header.track = read_fm_byte(pos + 1);
                crc = ibm_disc_format::crc_add_byte(crc, header.track);

                header.side = read_fm_byte(pos + 2);
                crc = ibm_disc_format::crc_add_byte(crc, header.side);

                header.sector = read_fm_byte(pos + 3);
                crc = ibm_disc_format::crc_add_byte(crc, header.sector);

                header.size_code = read_fm_byte(pos + 4);
                crc = ibm_disc_format::crc_add_byte(crc, header.size_code);

                uint8_t crc_hi = read_fm_byte(pos + 5);
                uint8_t crc_lo = read_fm_byte(pos + 6);
                header.header_crc = (static_cast<uint16_t>(crc_hi) << 8) | crc_lo;
                header.has_header_crc_error = (crc != header.header_crc);

                // Search for data mark following the ID field.
                // It should be within the GAP2 region (after FF gap + 00 sync).
                uint32_t search_start = pos + 7;
                uint32_t search_limit = std::min(search_start + 50, length);
                bool found_data = false;

                for (uint32_t dp = search_start; dp < search_limit; ++dp) {
                    auto [dc, dd, di] = ibm_disc_format::fm_from_2us_pulses(
                        track_.read_pulses(dp));

                    if (dc == ibm_disc_format::k_mark_clock_pattern) {
                        if (dd == ibm_disc_format::k_data_mark_data_pattern) {
                            header.is_deleted = false;
                            header.has_data_field = true;
                            header.data_pulse_position = dp + 1;
                            found_data = true;
                            break;
                        } else if (dd == ibm_disc_format::k_deleted_data_mark_data_pattern) {
                            header.is_deleted = true;
                            header.has_data_field = true;
                            header.data_pulse_position = dp + 1;
                            found_data = true;
                            break;
                        }
                    }
                }

                // If we found a data field, verify its CRC
                if (found_data) {
                    uint32_t data_len = header.data_byte_length();
                    uint32_t data_end = header.data_pulse_position + data_len + 2;  // +2 for CRC

                    if (data_end <= length) {
                        uint16_t data_crc = ibm_disc_format::crc_init(false);
                        // Include the data address mark in CRC
                        data_crc = ibm_disc_format::crc_add_byte(data_crc,
                            header.is_deleted ? ibm_disc_format::k_deleted_data_mark_data_pattern
                                              : ibm_disc_format::k_data_mark_data_pattern);

                        for (uint32_t i = 0; i < data_len; ++i) {
                            uint8_t byte = read_fm_byte_at(header.data_pulse_position + i);
                            data_crc = ibm_disc_format::crc_add_byte(data_crc, byte);
                        }

                        uint8_t dcrc_hi = read_fm_byte_at(header.data_pulse_position + data_len);
                        uint8_t dcrc_lo = read_fm_byte_at(header.data_pulse_position + data_len + 1);
                        header.data_crc = (static_cast<uint16_t>(dcrc_hi) << 8) | dcrc_lo;
                        header.has_data_crc_error = (data_crc != header.data_crc);
                    }
                }

                sectors.push_back(header);

                // Skip past this sector's data to avoid re-detecting
                if (found_data) {
                    pos = header.data_pulse_position + header.data_byte_length() + 2;
                } else {
                    pos += 7;
                }
            } else {
                ++pos;
            }
        }

        return sectors;
    }

    // Read sector data bytes from a previously found sector header.
    // Returns the number of bytes actually read.
    uint32_t read_sector_data(const SectorHeader& header,
                               std::span<uint8_t> buffer) const {
        if (!header.has_data_field) return 0;

        uint32_t data_len = std::min(
            static_cast<uint32_t>(buffer.size()),
            header.data_byte_length());
        uint32_t end = header.data_pulse_position + data_len;

        if (end > track_.length()) {
            data_len = track_.length() - header.data_pulse_position;
        }

        for (uint32_t i = 0; i < data_len; ++i) {
            buffer[i] = read_fm_byte_at(header.data_pulse_position + i);
        }

        return data_len;
    }

private:
    uint8_t read_fm_byte_at(uint32_t position) const {
        auto [c, d, i] = ibm_disc_format::fm_from_2us_pulses(
            track_.read_pulses(position));
        return d;
    }

    const DiscTrack& track_;
};

} // namespace beebium
