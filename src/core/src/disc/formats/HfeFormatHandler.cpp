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

#include "beebium/disc/formats/HfeFormatHandler.hpp"
#include "beebium/disc/IbmDiscFormat.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace beebium {

using namespace ibm_disc_format;

// HFE header signatures
static constexpr char k_hfe_v1_sig[] = "HXCPICFE";
static constexpr char k_hfe_v3_sig[] = "HXCHFEV3";
static constexpr size_t k_sig_len = 8;

// HFE header offsets
static constexpr size_t k_hdr_revision = 8;         // 1 byte
static constexpr size_t k_hdr_num_tracks = 9;        // 1 byte
static constexpr size_t k_hdr_num_sides = 10;        // 1 byte
static constexpr size_t k_hdr_track_list_offset = 18; // 2 bytes LE (in 512-byte blocks)

// HFE v3 opcodes (values after bit-flip)
static constexpr uint8_t k_v3_opcode_mask = 0xF0;
static constexpr uint8_t k_v3_nop = 0xF0;
static constexpr uint8_t k_v3_setindex = 0xF1;
static constexpr uint8_t k_v3_setbitrate = 0xF2;
static constexpr uint8_t k_v3_skipbits = 0xF3;
static constexpr uint8_t k_v3_rand = 0xF4;

// Format metadata layout:
// Bytes 0-511: track LUT (copied from file)
// Byte 512: version (1 or 3)
static constexpr size_t k_metadata_size = 513;
static constexpr size_t k_metadata_version_offset = 512;

// Reverse the bits in a byte (HFE stores all data bit-reversed).
static constexpr uint8_t byte_flip(uint8_t b) {
    b = static_cast<uint8_t>(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

// Read a 16-bit little-endian value from a byte buffer.
static uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Check if a flipped byte is a v3 opcode.
static bool is_v3_opcode(uint8_t flipped_byte) {
    return (flipped_byte & k_v3_opcode_mask) == k_v3_opcode_mask;
}

// De-interleave HFE track data for one side.
// HFE stores data in 512-byte blocks: first 256 bytes = side 0, last 256 = side 1.
static std::vector<uint8_t> deinterleave_side(const uint8_t* track_data,
                                                size_t track_byte_length,
                                                int side) {
    // Each side gets half the bytes
    size_t side_len = track_byte_length / 2;
    std::vector<uint8_t> result(side_len);

    for (size_t i = 0; i < side_len; ++i) {
        size_t block = i / 256;
        size_t offset_in_block = i % 256;
        size_t file_offset = block * 512 + (side == 1 ? 256 : 0) + offset_in_block;
        if (file_offset < track_byte_length) {
            result[i] = track_data[file_offset];
        }
    }

    return result;
}

// Decode HFE side data (bit-flipped bytes) into pulse words.
// Handles v3 opcodes if is_v3 is true.
static void decode_hfe_to_pulses(DiscTrack& track,
                                   const std::vector<uint8_t>& side_data,
                                   bool is_v3) {
    uint32_t pulse_index = 0;
    uint32_t bit_index = 0;
    uint32_t current_pulse = 0;

    size_t i = 0;
    while (i < side_data.size() && pulse_index < k_max_pulse_positions_per_track) {
        uint8_t flipped = byte_flip(side_data[i]);

        if (is_v3 && is_v3_opcode(flipped)) {
            switch (flipped) {
                case k_v3_nop:
                    ++i;
                    break;
                case k_v3_setindex:
                    ++i;
                    break;
                case k_v3_setbitrate:
                    i += 2;  // Skip opcode + bitrate byte
                    break;
                case k_v3_skipbits: {
                    if (i + 2 >= side_data.size()) { i = side_data.size(); break; }
                    uint8_t count = byte_flip(side_data[i + 1]);
                    uint8_t data_byte = byte_flip(side_data[i + 2]);
                    // Only the top 'count' bits are valid
                    for (uint8_t b = 0; b < count && b < 8; ++b) {
                        current_pulse <<= 1;
                        if (data_byte & (0x80 >> b)) {
                            current_pulse |= 1;
                        }
                        ++bit_index;
                        if (bit_index == 32) {
                            track.pulses()[pulse_index++] = current_pulse;
                            current_pulse = 0;
                            bit_index = 0;
                            if (pulse_index >= k_max_pulse_positions_per_track) break;
                        }
                    }
                    i += 3;
                    break;
                }
                case k_v3_rand:
                    // Weak bits: store as all zeros (no flux)
                    for (int b = 0; b < 8; ++b) {
                        current_pulse <<= 1;
                        ++bit_index;
                        if (bit_index == 32) {
                            track.pulses()[pulse_index++] = current_pulse;
                            current_pulse = 0;
                            bit_index = 0;
                            if (pulse_index >= k_max_pulse_positions_per_track) break;
                        }
                    }
                    ++i;
                    break;
                default:
                    ++i;  // Unknown opcode, skip
                    break;
            }
        } else {
            // Normal data byte: 8 flux bits
            for (int b = 7; b >= 0; --b) {
                current_pulse <<= 1;
                if (flipped & (1 << b)) {
                    current_pulse |= 1;
                }
                ++bit_index;
                if (bit_index == 32) {
                    track.pulses()[pulse_index++] = current_pulse;
                    current_pulse = 0;
                    bit_index = 0;
                    if (pulse_index >= k_max_pulse_positions_per_track) break;
                }
            }
            ++i;
        }
    }

    // Flush any remaining bits
    if (bit_index > 0 && pulse_index < k_max_pulse_positions_per_track) {
        current_pulse <<= (32 - bit_index);
        track.pulses()[pulse_index++] = current_pulse;
    }

    track.set_length(pulse_index);
}

// Encode pulse words into HFE side data (bit-flipped bytes) for write-back.
// Returns v3-format data with opcodes.
static std::vector<uint8_t> encode_pulses_to_hfe_v3(const DiscTrack& track) {
    std::vector<uint8_t> result;
    // Header: setindex + setbitrate + 72 (250kbps)
    result.push_back(byte_flip(k_v3_setindex));
    result.push_back(byte_flip(k_v3_setbitrate));
    result.push_back(byte_flip(72));

    uint32_t length = track.length();
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t pulse = track.read_pulses(i);

        if (pulse == 0) {
            // Zero pulse = weak bits -> 4 rand opcodes (one per byte = 8 bits x 4 = 32 bits)
            for (int j = 0; j < 4; ++j) {
                result.push_back(byte_flip(k_v3_rand));
            }
        } else {
            // Convert 32-bit pulse word to 4 bytes (MSB first)
            for (int j = 3; j >= 0; --j) {
                uint8_t byte_val = static_cast<uint8_t>((pulse >> (j * 8)) & 0xFF);
                uint8_t flipped = byte_flip(byte_val);
                // Check if the flipped byte accidentally looks like an opcode
                if (is_v3_opcode(byte_flip(flipped))) {
                    // Replace with rand to avoid opcode collision
                    result.push_back(byte_flip(k_v3_rand));
                } else {
                    result.push_back(flipped);
                }
            }
        }
    }

    return result;
}

// Interleave side data back into HFE track block format.
static void interleave_and_write(std::fstream& file,
                                   size_t track_file_offset,
                                   size_t track_byte_length,
                                   const std::vector<uint8_t>& side0_data,
                                   const std::vector<uint8_t>& side1_data) {
    size_t side_len = track_byte_length / 2;
    std::vector<uint8_t> interleaved(track_byte_length, 0);

    auto write_side = [&](const std::vector<uint8_t>& data, int side) {
        size_t len = std::min(data.size(), side_len);
        for (size_t i = 0; i < len; ++i) {
            size_t block = i / 256;
            size_t offset = i % 256;
            size_t pos = block * 512 + (side == 1 ? 256 : 0) + offset;
            if (pos < interleaved.size()) {
                interleaved[pos] = data[i];
            }
        }
    };

    write_side(side0_data, 0);
    write_side(side1_data, 1);

    file.seekp(static_cast<std::streamoff>(track_file_offset));
    file.write(reinterpret_cast<const char*>(interleaved.data()),
               static_cast<std::streamsize>(interleaved.size()));
}

// Write-back callback for HFE format.
static void hfe_write_track_callback(Disc& disc, bool upper_side, uint32_t track_number) {
    const auto& filepath = disc.source_filepath();
    if (filepath.empty()) return;

    auto metadata = disc.format_metadata();
    if (metadata.size() < k_metadata_size) return;

    // Look up track offset and length from LUT
    size_t lut_entry = track_number * 4;
    if (lut_entry + 4 > 512) return;

    uint16_t block_offset = read_le16(metadata.data() + lut_entry);
    uint16_t byte_length = read_le16(metadata.data() + lut_entry + 2);
    size_t track_file_offset = static_cast<size_t>(block_offset) * 512;

    if (byte_length == 0) return;

    // Read existing track data from file to preserve the other side
    std::fstream file(filepath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) return;

    std::vector<uint8_t> existing(byte_length);
    file.seekg(static_cast<std::streamoff>(track_file_offset));
    file.read(reinterpret_cast<char*>(existing.data()), byte_length);

    // Deinterleave existing data for both sides
    auto existing_side0 = deinterleave_side(existing.data(), byte_length, 0);
    auto existing_side1 = deinterleave_side(existing.data(), byte_length, 1);

    // Encode the modified side
    auto new_data = encode_pulses_to_hfe_v3(disc.track(upper_side, track_number));

    if (upper_side) {
        interleave_and_write(file, track_file_offset, byte_length, existing_side0, new_data);
    } else {
        interleave_and_write(file, track_file_offset, byte_length, new_data, existing_side1);
    }
}

std::string_view HfeFormatHandler::format_name() const {
    return "HFE (HxC Floppy Emulator)";
}

std::string_view HfeFormatHandler::format_description() const {
    return "HxC Floppy Emulator flux-level disc image";
}

std::vector<std::string_view> HfeFormatHandler::file_extensions() const {
    return {".hfe"};
}

FormatDetectionResult HfeFormatHandler::detect(std::span<const uint8_t> file_data,
                                                 std::string_view extension) const {
    if (file_data.size() < 512) return {};

    // Check magic signature (highest priority, ignores extension)
    bool is_v1 = std::memcmp(file_data.data(), k_hfe_v1_sig, k_sig_len) == 0;
    bool is_v3 = std::memcmp(file_data.data(), k_hfe_v3_sig, k_sig_len) == 0;

    if (is_v1 || is_v3) {
        // Validate revision
        if (file_data[k_hdr_revision] != 0) return {};

        return {true, 100, is_v3 ? "HFE v3" : "HFE v1"};
    }

    // Extension-only match with low confidence (file might be HFE but header check failed)
    if (extension == ".hfe") {
        return {true, 30, "HFE (unverified)"};
    }

    return {};
}

DiscLoadResult HfeFormatHandler::load(std::span<const uint8_t> file_data,
                                        const std::filesystem::path& source_filepath) const {
    if (file_data.size() < 512) {
        return {nullptr, "HFE file too small for header"};
    }

    // Validate signature
    bool is_v1 = std::memcmp(file_data.data(), k_hfe_v1_sig, k_sig_len) == 0;
    bool is_v3 = std::memcmp(file_data.data(), k_hfe_v3_sig, k_sig_len) == 0;

    if (!is_v1 && !is_v3) {
        return {nullptr, "Not a valid HFE file: unrecognised signature"};
    }

    if (file_data[k_hdr_revision] != 0) {
        return {nullptr, "Unsupported HFE revision: " + std::to_string(file_data[k_hdr_revision])};
    }

    uint8_t num_tracks = file_data[k_hdr_num_tracks];
    uint8_t num_sides = file_data[k_hdr_num_sides];

    if (num_sides < 1 || num_sides > 2) {
        return {nullptr, "Invalid HFE side count: " + std::to_string(num_sides)};
    }
    if (num_tracks == 0 || num_tracks > 84) {
        return {nullptr, "Invalid HFE track count: " + std::to_string(num_tracks)};
    }

    uint16_t lut_block = read_le16(file_data.data() + k_hdr_track_list_offset);
    size_t lut_offset = static_cast<size_t>(lut_block) * 512;

    if (lut_offset + 512 > file_data.size()) {
        return {nullptr, "HFE track list offset beyond end of file"};
    }

    // Create disc
    auto disc = std::make_unique<Disc>();
    disc->set_name(source_filepath.filename().string());
    disc->set_double_sided(num_sides == 2);
    disc->set_source_filepath(source_filepath);
    disc->set_format_name(is_v3 ? "HFE v3" : "HFE v1");

    // Store format metadata: 512-byte LUT + 1-byte version
    disc->allocate_format_metadata(k_metadata_size);
    auto metadata = disc->format_metadata();
    std::memcpy(metadata.data(), file_data.data() + lut_offset, 512);
    metadata[k_metadata_version_offset] = is_v3 ? 3 : 1;

    // Check write protection
    std::error_code ec;
    auto perms = std::filesystem::status(source_filepath, ec).permissions();
    if (!ec && (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
        disc->set_write_protected(true);
    }

    // Load each track
    for (uint8_t t = 0; t < num_tracks; ++t) {
        size_t entry_offset = t * 4;
        uint16_t block_offset = read_le16(metadata.data() + entry_offset);
        uint16_t byte_length = read_le16(metadata.data() + entry_offset + 2);

        size_t track_file_offset = static_cast<size_t>(block_offset) * 512;

        if (track_file_offset + byte_length > file_data.size()) continue;
        if (byte_length == 0) continue;

        const uint8_t* track_data = file_data.data() + track_file_offset;

        for (uint8_t s = 0; s < num_sides; ++s) {
            auto side_data = deinterleave_side(track_data, byte_length, s);
            bool upper_side = (s == 1);
            decode_hfe_to_pulses(disc->track(upper_side, t), side_data, is_v3);
        }
    }

    // Set write-back callback
    if (!disc->is_write_protected()) {
        disc->set_write_track_callback(hfe_write_track_callback);
    }

    return {std::move(disc), ""};
}

bool HfeFormatHandler::supports_write() const {
    return true;
}

} // namespace beebium
