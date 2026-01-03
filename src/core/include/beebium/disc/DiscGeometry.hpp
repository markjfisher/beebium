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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace beebium {

// Disc image format types
enum class DiscFormat {
    SSD,      // Single-sided disc (DFS)
    DSD,      // Double-sided disc (DFS) - interleaved track layout
    Unknown
};

// Disc geometry describes the physical layout of a disc image.
//
// DFS discs use 256-byte sectors, 10 sectors per track.
// SSD (single-sided): 40 or 80 tracks on one side
// DSD (double-sided): 40 or 80 tracks, interleaved (T0S0, T0S1, T1S0, T1S1, ...)
struct DiscGeometry {
    DiscFormat format = DiscFormat::Unknown;
    uint8_t sides = 0;              // 1 for SSD, 2 for DSD
    uint8_t tracks_per_side = 0;    // 40 or 80
    uint8_t sectors_per_track = 0;  // 10 for DFS
    uint16_t sector_size = 0;       // 256 for DFS

    // Calculate total disc image size in bytes
    constexpr size_t total_size() const {
        return static_cast<size_t>(sides) * tracks_per_side * sectors_per_track * sector_size;
    }

    // Calculate byte offset within image for a given sector.
    // Returns offset, or std::nullopt if parameters are out of range.
    std::optional<size_t> sector_offset(uint8_t side, uint8_t track, uint8_t sector) const;

    // Detect geometry from file size and extension.
    // Returns std::nullopt if format cannot be determined.
    static std::optional<DiscGeometry> detect_from_size(size_t file_size, std::string_view extension);
};

} // namespace beebium
