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

#include <beebium/disc/DiscGeometry.hpp>

namespace beebium {

std::optional<size_t> DiscGeometry::sector_offset(uint8_t side, uint8_t track, uint8_t sector) const {
    // Validate parameters
    if (side >= sides) {
        return std::nullopt;
    }
    if (track >= tracks_per_side) {
        return std::nullopt;
    }
    if (sector >= sectors_per_track) {
        return std::nullopt;
    }

    size_t offset = 0;
    const size_t bytes_per_track = static_cast<size_t>(sectors_per_track) * sector_size;

    if (format == DiscFormat::SSD) {
        // Linear layout: track 0 sectors, track 1 sectors, ...
        offset = static_cast<size_t>(track) * bytes_per_track +
                 static_cast<size_t>(sector) * sector_size;
    } else if (format == DiscFormat::DSD) {
        // Interleaved layout: track 0 side 0, track 0 side 1, track 1 side 0, track 1 side 1, ...
        // Each track pair (side 0 + side 1) occupies 2 * bytes_per_track
        offset = static_cast<size_t>(track) * 2 * bytes_per_track +  // Track position
                 static_cast<size_t>(side) * bytes_per_track +       // Side within track
                 static_cast<size_t>(sector) * sector_size;          // Sector within side
    } else {
        return std::nullopt;  // Unknown format
    }

    return offset;
}

std::optional<DiscGeometry> DiscGeometry::detect_from_size(size_t file_size, std::string_view extension) {
    // Normalize extension to lowercase for comparison
    std::string ext_lower;
    ext_lower.reserve(extension.size());
    for (char c : extension) {
        ext_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // DFS format constants
    constexpr uint8_t DFS_SECTORS_PER_TRACK = 10;
    constexpr uint16_t DFS_SECTOR_SIZE = 256;
    constexpr size_t BYTES_PER_TRACK = DFS_SECTORS_PER_TRACK * DFS_SECTOR_SIZE;  // 2560

    // SSD: Single-sided disc
    if (ext_lower == ".ssd") {
        // 40-track single-sided: 40 * 10 * 256 = 102,400 bytes
        constexpr size_t SSD_40_SIZE = 40 * BYTES_PER_TRACK;
        if (file_size == SSD_40_SIZE) {
            return DiscGeometry{
                .format = DiscFormat::SSD,
                .sides = 1,
                .tracks_per_side = 40,
                .sectors_per_track = DFS_SECTORS_PER_TRACK,
                .sector_size = DFS_SECTOR_SIZE
            };
        }

        // 80-track single-sided: 80 * 10 * 256 = 204,800 bytes
        constexpr size_t SSD_80_SIZE = 80 * BYTES_PER_TRACK;
        if (file_size == SSD_80_SIZE) {
            return DiscGeometry{
                .format = DiscFormat::SSD,
                .sides = 1,
                .tracks_per_side = 80,
                .sectors_per_track = DFS_SECTORS_PER_TRACK,
                .sector_size = DFS_SECTOR_SIZE
            };
        }
    }

    // DSD: Double-sided disc (interleaved)
    if (ext_lower == ".dsd") {
        // 80-track double-sided: 2 * 80 * 10 * 256 = 409,600 bytes
        constexpr size_t DSD_80_SIZE = 2 * 80 * BYTES_PER_TRACK;
        if (file_size == DSD_80_SIZE) {
            return DiscGeometry{
                .format = DiscFormat::DSD,
                .sides = 2,
                .tracks_per_side = 80,
                .sectors_per_track = DFS_SECTORS_PER_TRACK,
                .sector_size = DFS_SECTOR_SIZE
            };
        }

        // 40-track double-sided: 2 * 40 * 10 * 256 = 204,800 bytes
        constexpr size_t DSD_40_SIZE = 2 * 40 * BYTES_PER_TRACK;
        if (file_size == DSD_40_SIZE) {
            return DiscGeometry{
                .format = DiscFormat::DSD,
                .sides = 2,
                .tracks_per_side = 40,
                .sectors_per_track = DFS_SECTORS_PER_TRACK,
                .sector_size = DFS_SECTOR_SIZE
            };
        }
    }

    return std::nullopt;
}

} // namespace beebium
