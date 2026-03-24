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

// Format handler for ADFS (Advanced Disc Filing System) sector images.
//
// Supports:
//   .adl - ADFS Large: 80 tracks, 2 sides, 16 sectors/track, 256 bytes/sector (655360 bytes)
//   .adf - ADFS auto-detect: determines geometry from file size and content
//   .adm - ADFS Medium: 80 tracks, 1 side, 16 sectors/track (327680 bytes)
//   .ads - ADFS Small: 40 tracks, 1 side, 16 sectors/track (163840 bytes)
//
// All ADFS formats use MFM (double-density) encoding.
class AdfsFormatHandler : public DiscFormatHandler {
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
