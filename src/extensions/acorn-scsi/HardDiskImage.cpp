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

#include "HardDiskImage.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace beebium {

HardDiskImage::~HardDiskImage() {
    if (dat_file_) {
        std::fclose(dat_file_);
    }
}

std::filesystem::path HardDiskImage::derive_dsc_filepath(
        const std::filesystem::path& dat_filepath) {
    auto dsc = dat_filepath;
    dsc.replace_extension(".dsc");
    return dsc;
}

std::unique_ptr<HardDiskImage> HardDiskImage::open(
        const std::filesystem::path& dat_filepath) {
    if (!std::filesystem::exists(dat_filepath)) {
        throw std::runtime_error("DAT file not found: " + dat_filepath.string());
    }

    auto image = std::unique_ptr<HardDiskImage>(new HardDiskImage());
    image->dat_filepath_ = dat_filepath;
    image->dsc_filepath_ = derive_dsc_filepath(dat_filepath);

    // Check write protection from filesystem permissions
    auto perms = std::filesystem::status(dat_filepath).permissions();
    image->write_protected_ =
        (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none;

    // Open DAT file
    image->dat_file_ = std::fopen(dat_filepath.string().c_str(),
                                   image->write_protected_ ? "rb" : "r+b");
    if (!image->dat_file_) {
        throw std::runtime_error("Cannot open DAT file: " + dat_filepath.string());
    }

    // Load or generate DSC
    if (std::filesystem::exists(image->dsc_filepath_)) {
        FILE* dsc_file = std::fopen(image->dsc_filepath_.string().c_str(), "rb");
        if (dsc_file) {
            std::fread(image->dsc_data_.data(), 1, kDscSize, dsc_file);
            std::fclose(dsc_file);
        }
    } else {
        auto file_size = std::filesystem::file_size(dat_filepath);
        image->generate_default_dsc(file_size);
    }

    return image;
}

std::unique_ptr<HardDiskImage> HardDiskImage::create(
        const std::filesystem::path& dat_filepath,
        uint16_t cylinders, uint8_t heads) {
    // Calculate total size
    uint64_t total = static_cast<uint64_t>(cylinders) * heads * kSectorsPerTrack
                     * scsi::ACORN_BLOCK_SIZE;

    // Create zero-filled DAT file
    {
        FILE* f = std::fopen(dat_filepath.string().c_str(), "wb");
        if (!f) {
            throw std::runtime_error("Cannot create DAT file: " + dat_filepath.string());
        }
        // Write zeros in chunks
        std::array<uint8_t, 4096> zeros{};
        uint64_t remaining = total;
        while (remaining > 0) {
            size_t chunk = std::min(remaining, static_cast<uint64_t>(zeros.size()));
            std::fwrite(zeros.data(), 1, chunk, f);
            remaining -= chunk;
        }
        std::fclose(f);
    }

    // Build DSC data
    auto image = std::unique_ptr<HardDiskImage>(new HardDiskImage());
    image->dat_filepath_ = dat_filepath;
    image->dsc_filepath_ = derive_dsc_filepath(dat_filepath);
    image->dsc_data_.fill(0);
    image->dsc_data_[3] = 0x80;                                       // Speed flag
    image->dsc_data_[10] = 1;                                         // Removable flag
    image->dsc_data_[12] = 1;                                         // Stepping flag
    image->dsc_data_[13] = static_cast<uint8_t>((cylinders >> 8) & 0xFF);  // Cylinders high
    image->dsc_data_[14] = static_cast<uint8_t>(cylinders & 0xFF);         // Cylinders low
    image->dsc_data_[15] = heads;
    image->dsc_data_[17] = 0x80;                                      // RWCC high
    image->dsc_data_[19] = 0x80;                                      // Landing zone flag

    // Write DSC file
    image->write_dsc();

    // Open DAT for read/write
    image->dat_file_ = std::fopen(dat_filepath.string().c_str(), "r+b");
    if (!image->dat_file_) {
        throw std::runtime_error("Cannot reopen DAT file: " + dat_filepath.string());
    }
    image->write_protected_ = false;

    return image;
}

bool HardDiskImage::read_sector(uint8_t* dest, uint32_t lba) {
    if (!is_valid_lba(lba) || !dat_file_) {
        return false;
    }

    long offset = static_cast<long>(lba) * scsi::ACORN_BLOCK_SIZE;
    if (std::fseek(dat_file_, offset, SEEK_SET) != 0) {
        return false;
    }

    size_t read = std::fread(dest, 1, scsi::ACORN_BLOCK_SIZE, dat_file_);
    if (read < scsi::ACORN_BLOCK_SIZE) {
        // Pad with zeros if reading past end of file
        std::memset(dest + read, 0, scsi::ACORN_BLOCK_SIZE - read);
    }
    return true;
}

bool HardDiskImage::write_sector(const uint8_t* src, uint32_t lba) {
    if (!is_valid_lba(lba) || !dat_file_ || write_protected_) {
        return false;
    }

    long offset = static_cast<long>(lba) * scsi::ACORN_BLOCK_SIZE;
    if (std::fseek(dat_file_, offset, SEEK_SET) != 0) {
        return false;
    }

    size_t written = std::fwrite(src, 1, scsi::ACORN_BLOCK_SIZE, dat_file_);
    std::fflush(dat_file_);
    return written == scsi::ACORN_BLOCK_SIZE;
}

bool HardDiskImage::is_valid_lba(uint32_t lba) const {
    return lba < total_sectors();
}

uint32_t HardDiskImage::total_sectors() const {
    return static_cast<uint32_t>(cylinders()) * heads() * kSectorsPerTrack;
}

uint16_t HardDiskImage::cylinders() const {
    return (static_cast<uint16_t>(dsc_data_[13]) << 8) | dsc_data_[14];
}

uint8_t HardDiskImage::heads() const {
    return dsc_data_[15];
}

void HardDiskImage::set_device_parameters(std::span<const uint8_t> params) {
    size_t copy_size = std::min(params.size(), static_cast<size_t>(kDscSize));
    std::copy_n(params.begin(), copy_size, dsc_data_.begin());
    write_dsc();
}

void HardDiskImage::format() {
    if (!dat_file_ || write_protected_) {
        return;
    }

    // Truncate by reopening in write mode, then reopen for read/write
    std::fclose(dat_file_);
    dat_file_ = nullptr;

    uint64_t total_bytes = static_cast<uint64_t>(total_sectors()) * scsi::ACORN_BLOCK_SIZE;

    {
        FILE* f = std::fopen(dat_filepath_.string().c_str(), "wb");
        if (f) {
            std::array<uint8_t, 4096> zeros{};
            uint64_t remaining = total_bytes;
            while (remaining > 0) {
                size_t chunk = std::min(remaining, static_cast<uint64_t>(zeros.size()));
                std::fwrite(zeros.data(), 1, chunk, f);
                remaining -= chunk;
            }
            std::fclose(f);
        }
    }

    dat_file_ = std::fopen(dat_filepath_.string().c_str(), "r+b");
}

void HardDiskImage::generate_default_dsc(uint64_t file_size) {
    dsc_data_.fill(0);
    dsc_data_[3] = 0x80;
    dsc_data_[10] = 1;
    dsc_data_[12] = 1;
    dsc_data_[17] = 0x80;
    dsc_data_[19] = 0x80;

    // Calculate geometry: default 10 heads, derive cylinders from file size
    uint8_t default_heads = 10;
    uint32_t sectors = static_cast<uint32_t>(file_size / scsi::ACORN_BLOCK_SIZE);
    uint16_t cyl = static_cast<uint16_t>(
        sectors / (default_heads * kSectorsPerTrack));
    if (cyl == 0 && sectors > 0) cyl = 1;

    dsc_data_[13] = static_cast<uint8_t>((cyl >> 8) & 0xFF);
    dsc_data_[14] = static_cast<uint8_t>(cyl & 0xFF);
    dsc_data_[15] = default_heads;
}

void HardDiskImage::write_dsc() {
    FILE* f = std::fopen(dsc_filepath_.string().c_str(), "wb");
    if (f) {
        std::fwrite(dsc_data_.data(), 1, kDscSize, f);
        std::fclose(f);
    }
}

}  // namespace beebium
