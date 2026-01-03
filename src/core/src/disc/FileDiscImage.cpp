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

#include <beebium/disc/FileDiscImage.hpp>
#include <fstream>
#include <iterator>

namespace beebium {

FileDiscImage::FileDiscImage(std::filesystem::path filepath, DiscGeometry geometry,
                             std::vector<uint8_t> data)
    : filepath_(std::move(filepath))
    , geometry_(geometry)
    , data_(std::move(data))
{
}

std::optional<std::unique_ptr<FileDiscImage>> FileDiscImage::load(const std::filesystem::path& filepath) {
    // Check if file exists
    if (!std::filesystem::exists(filepath)) {
        return std::nullopt;
    }

    // Read file contents
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::vector<uint8_t> data(std::istreambuf_iterator<char>(file), {});

    // Detect geometry from file size and extension
    std::string extension = filepath.extension().string();
    auto geometry = DiscGeometry::detect_from_size(data.size(), extension);
    if (!geometry.has_value()) {
        return std::nullopt;
    }

    // Create the image (using a helper since constructor is private)
    return std::unique_ptr<FileDiscImage>(
        new FileDiscImage(filepath, *geometry, std::move(data))
    );
}

std::unique_ptr<FileDiscImage> FileDiscImage::create(const std::filesystem::path& filepath,
                                                      const DiscGeometry& geometry) {
    std::vector<uint8_t> data(geometry.total_size(), 0);
    return std::unique_ptr<FileDiscImage>(
        new FileDiscImage(filepath, geometry, std::move(data))
    );
}

std::string FileDiscImage::name() const {
    return filepath_.filename().string();
}

bool FileDiscImage::is_write_protected() const {
    return write_protected_;
}

void FileDiscImage::set_write_protected(bool write_protected) {
    write_protected_ = write_protected;
}

uint8_t FileDiscImage::sides() const {
    return geometry_.sides;
}

uint8_t FileDiscImage::tracks_per_side() const {
    return geometry_.tracks_per_side;
}

uint8_t FileDiscImage::sectors_per_track() const {
    return geometry_.sectors_per_track;
}

uint16_t FileDiscImage::sector_size() const {
    return geometry_.sector_size;
}

bool FileDiscImage::read_sector(uint8_t side, uint8_t track, uint8_t sector,
                                std::span<uint8_t> buffer) {
    auto offset = geometry_.sector_offset(side, track, sector);
    if (!offset.has_value()) {
        return false;
    }

    if (buffer.size() < geometry_.sector_size) {
        return false;
    }

    std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(*offset),
                geometry_.sector_size,
                buffer.begin());
    return true;
}

bool FileDiscImage::write_sector(uint8_t side, uint8_t track, uint8_t sector,
                                 std::span<const uint8_t> buffer) {
    if (write_protected_) {
        return false;
    }

    auto offset = geometry_.sector_offset(side, track, sector);
    if (!offset.has_value()) {
        return false;
    }

    if (buffer.size() < geometry_.sector_size) {
        return false;
    }

    // Update in-memory copy
    std::copy_n(buffer.begin(),
                geometry_.sector_size,
                data_.begin() + static_cast<std::ptrdiff_t>(*offset));

    // Write through to file immediately
    std::fstream file(filepath_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        return false;
    }
    file.seekp(static_cast<std::streamoff>(*offset));
    file.write(reinterpret_cast<const char*>(buffer.data()),
               static_cast<std::streamsize>(geometry_.sector_size));
    return file.good();
}

void FileDiscImage::flush() {
    // No-op: writes go directly to file
}

} // namespace beebium
