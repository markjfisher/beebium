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

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace beebium {

// In-memory disc image for testing and debugging.
//
// No backing file - all data exists only in memory.
// Useful for unit testing disc controllers in isolation.
class MemoryDiscImage : public DiscImage {
public:
    // Create an empty disc with the given geometry
    static std::unique_ptr<MemoryDiscImage> create(const DiscGeometry& geometry,
                                                    std::string name = "memory") {
        return std::unique_ptr<MemoryDiscImage>(
            new MemoryDiscImage(geometry, std::move(name)));
    }

    // Create a standard 80-track single-sided disc (SSD format)
    static std::unique_ptr<MemoryDiscImage> create_ssd(std::string name = "memory.ssd") {
        DiscGeometry geometry{
            .format = DiscFormat::SSD,
            .sides = 1,
            .tracks_per_side = 80,
            .sectors_per_track = 10,
            .sector_size = 256
        };
        return create(geometry, std::move(name));
    }

    // Create a standard 80-track double-sided disc (DSD format)
    static std::unique_ptr<MemoryDiscImage> create_dsd(std::string name = "memory.dsd") {
        DiscGeometry geometry{
            .format = DiscFormat::DSD,
            .sides = 2,
            .tracks_per_side = 80,
            .sectors_per_track = 10,
            .sector_size = 256
        };
        return create(geometry, std::move(name));
    }

    ~MemoryDiscImage() override = default;

    // DiscImage interface
    std::string name() const override { return name_; }

    bool is_write_protected() const override { return write_protected_; }
    void set_write_protected(bool write_protected) override { write_protected_ = write_protected; }

    uint8_t sides() const override { return geometry_.sides; }
    uint8_t tracks_per_side() const override { return geometry_.tracks_per_side; }
    uint8_t sectors_per_track() const override { return geometry_.sectors_per_track; }
    uint16_t sector_size() const override { return geometry_.sector_size; }

    bool read_sector(uint8_t side, uint8_t track, uint8_t sector,
                    std::span<uint8_t> buffer) override {
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

    bool write_sector(uint8_t side, uint8_t track, uint8_t sector,
                     std::span<const uint8_t> buffer) override {
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

        std::copy_n(buffer.begin(),
                    geometry_.sector_size,
                    data_.begin() + static_cast<std::ptrdiff_t>(*offset));
        return true;
    }

    void flush() override {
        // No-op: memory only, nothing to flush
    }

    // Direct data access for testing
    std::span<uint8_t> data() { return data_; }
    std::span<const uint8_t> data() const { return data_; }

private:
    MemoryDiscImage(DiscGeometry geometry, std::string name)
        : geometry_(geometry)
        , name_(std::move(name))
        , data_(geometry.total_size(), 0) {}

    DiscGeometry geometry_;
    std::string name_;
    std::vector<uint8_t> data_;
    bool write_protected_ = false;
};

} // namespace beebium
