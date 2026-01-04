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

#include "DiscImage.hpp"
#include "DiscGeometry.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace beebium {

// File-based disc image implementation.
//
// Loads the entire disc image into memory for fast access.
// Changes are buffered in memory until flush() is called.
// If the image is not flushed before destruction, changes are lost.
class FileDiscImage : public DiscImage {
public:
    // Load a disc image from file.
    // Throws std::runtime_error if the file cannot be read or format is unrecognized.
    // Automatically detects write-protection from filesystem permissions.
    static std::unique_ptr<FileDiscImage> load(const std::filesystem::path& filepath);

    // Create a new empty disc image with the given geometry.
    // The file is not created until flush() is called.
    static std::unique_ptr<FileDiscImage> create(const std::filesystem::path& filepath,
                                                  const DiscGeometry& geometry);

    ~FileDiscImage() override = default;

    // DiscImage interface
    std::string name() const override;
    bool is_write_protected() const override;
    void set_write_protected(bool write_protected) override;

    uint8_t sides() const override;
    uint8_t tracks_per_side() const override;
    uint8_t sectors_per_track() const override;
    uint16_t sector_size() const override;

    bool read_sector(uint8_t side, uint8_t track, uint8_t sector,
                    std::span<uint8_t> buffer) override;
    bool write_sector(uint8_t side, uint8_t track, uint8_t sector,
                     std::span<const uint8_t> buffer) override;

    void flush() override;

    // Additional accessors
    const std::filesystem::path& filepath() const { return filepath_; }

private:
    FileDiscImage(std::filesystem::path filepath, DiscGeometry geometry,
                  std::vector<uint8_t> data);

    std::filesystem::path filepath_;
    DiscGeometry geometry_;
    std::vector<uint8_t> data_;
    bool write_protected_ = false;
};

} // namespace beebium
