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

#include "../DiscFormatHandler.hpp"

namespace beebium {

// Format handler for SSD (Single-Sided Disc) and DSD (Double-Sided Disc)
// images used by the Acorn DFS filing system.
//
// SSD: Linear sector layout. 10 sectors/track, 256 bytes/sector, 40 or 80 tracks.
// DSD: Interleaved layout. Side 0 track N sectors, then side 1 track N sectors.
//
// Supports truncated images (trailing empty sectors omitted) as produced by
// beebasm and similar tools. Missing sectors are zero-filled when building
// pulse tracks.
class SsdFormatHandler : public DiscFormatHandler {
public:
    std::string_view format_name() const override;
    std::vector<std::string_view> file_extensions() const override;
    FormatDetectionResult detect(std::span<const uint8_t> file_data,
                                  std::string_view extension) const override;
    DiscLoadResult load(std::span<const uint8_t> file_data,
                         const std::filesystem::path& source_filepath) const override;
    bool supports_write() const override;
};

} // namespace beebium
