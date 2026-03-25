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

// Format handler for HFE (HxC Floppy Emulator) disc images.
//
// Supports both HFE v1 (signature "HXCPICFE") and HFE v3 (signature "HXCHFEV3").
// HFE stores raw flux-level data with side-interleaved tracks in 512-byte blocks.
// All data bytes are bit-reversed (MSB/LSB swapped).
//
// HFE v3 adds an opcode system for weak bits (RAND), bitrate changes, and
// index pulse markers. This is the standard format for hardware disc emulators
// (Gotek, HxC) and the de facto preservation format for flux-level captures.
//
// Write-back uses HFE v3 format to preserve weak bit information.
class HfeFormatHandler : public DiscFormatHandler {
public:
    std::string_view format_name() const override;
    std::string_view format_description() const override;
    std::vector<std::string_view> file_extensions() const override;
    FormatDetectionResult detect(std::span<const uint8_t> file_data,
                                  std::string_view extension) const override;
    DiscLoadResult load(std::span<const uint8_t> file_data,
                         const std::filesystem::path& source_filepath) const override;
    bool supports_write() const override;
};

} // namespace beebium
