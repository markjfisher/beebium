// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_HARD_DISK_IMAGE_HPP
#define BEEBIUM_HARD_DISK_IMAGE_HPP

#include "ScsiConstants.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace beebium {

// Filesystem-agnostic block device backed by a DAT+DSC file pair.
//
// DAT file: raw 256-byte sectors at offset LBA * 256. No header.
// DSC file: 22-byte geometry descriptor (cylinders, heads, 33 SPT).
//
// A newly created image is all zeros ("unformatted"). ADFS formatting
// is done on the 6502 side via SCSI WRITE commands.
class HardDiskImage {
public:
    static constexpr size_t kDscSize = 22;
    static constexpr uint8_t kSectorsPerTrack = 33;

    ~HardDiskImage();

    // Open an existing DAT file. Loads DSC sidecar if present,
    // otherwise auto-generates geometry from file size.
    static std::unique_ptr<HardDiskImage> open(const std::filesystem::path& dat_filepath);

    // Create a new blank (zero-filled) disc image with the given geometry.
    // Creates both DAT and DSC files.
    static std::unique_ptr<HardDiskImage> create(const std::filesystem::path& dat_filepath,
                                                  uint16_t cylinders, uint8_t heads);

    // Sector I/O
    bool read_sector(uint8_t* dest, uint32_t lba);
    bool write_sector(const uint8_t* src, uint32_t lba);

    // LBA validation
    bool is_valid_lba(uint32_t lba) const;
    uint32_t total_sectors() const;

    // Geometry
    uint16_t cylinders() const;
    uint8_t heads() const;

    // Write protection (based on filesystem permissions)
    bool is_write_protected() const { return write_protected_; }

    // MODE SENSE: the raw 22-byte DSC data
    std::span<const uint8_t, kDscSize> device_parameters() const {
        return std::span<const uint8_t, kDscSize>(dsc_data_);
    }

    // MODE SELECT: update geometry from received parameter data
    void set_device_parameters(std::span<const uint8_t> params);

    // Low-level format: zero all sectors, preserving geometry
    void format();

    // File path accessors
    const std::filesystem::path& dat_filepath() const { return dat_filepath_; }
    const std::filesystem::path& dsc_filepath() const { return dsc_filepath_; }

private:
    HardDiskImage() = default;

    void generate_default_dsc(uint64_t file_size);
    void write_dsc();
    static std::filesystem::path derive_dsc_filepath(const std::filesystem::path& dat_filepath);

    std::filesystem::path dat_filepath_;
    std::filesystem::path dsc_filepath_;
    FILE* dat_file_ = nullptr;
    std::array<uint8_t, kDscSize> dsc_data_{};
    bool write_protected_ = false;
};

}  // namespace beebium

#endif  // BEEBIUM_HARD_DISK_IMAGE_HPP
