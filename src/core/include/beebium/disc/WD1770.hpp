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

#include <array>
#include <cstdint>

namespace beebium {

class DiscDrive;

// Western Digital WD1770 Floppy Disc Controller emulation.
//
// Register layout (offset 0-3):
//   0: Status register (read) / Command register (write)
//   1: Track register (read/write)
//   2: Sector register (read/write)
//   3: Data register (read/write)
//
// The WD1770 is used in the BBC Model B+ and as an aftermarket upgrade
// for the Model B. It generates INTRQ for command completion and DRQ
// for data transfer, both directly driving NMI via glue logic.
class WD1770 {
public:
    // Status register bits
    static constexpr uint8_t STATUS_BUSY        = 0x01;
    static constexpr uint8_t STATUS_DRQ         = 0x02;  // Type II/III only
    static constexpr uint8_t STATUS_INDEX       = 0x02;  // Type I only
    static constexpr uint8_t STATUS_LOST_DATA   = 0x04;
    static constexpr uint8_t STATUS_CRC_ERROR   = 0x08;
    static constexpr uint8_t STATUS_SEEK_ERROR  = 0x10;  // Type I only
    static constexpr uint8_t STATUS_RNF         = 0x10;  // Type II/III: Record Not Found
    static constexpr uint8_t STATUS_SPIN_UP     = 0x20;  // Type I only
    static constexpr uint8_t STATUS_RECORD_TYPE = 0x20;  // Type II/III only
    static constexpr uint8_t STATUS_WRITE_PROT  = 0x40;
    static constexpr uint8_t STATUS_MOTOR_ON    = 0x80;

    WD1770() = default;
    ~WD1770() = default;

    // Non-copyable
    WD1770(const WD1770&) = delete;
    WD1770& operator=(const WD1770&) = delete;

    // Register access (offset is masked to 2 bits)
    uint8_t read(uint16_t offset) {
        switch (offset & 0x03) {
            case 0: return status_;
            case 1: return track_;
            case 2: return sector_;
            case 3: return data_;
            default: return 0xFF;
        }
    }

    void write(uint16_t offset, uint8_t value) {
        switch (offset & 0x03) {
            case 0:
                // Command register - will be handled in Phase 5
                command_ = value;
                break;
            case 1:
                track_ = value;
                break;
            case 2:
                sector_ = value;
                break;
            case 3:
                data_ = value;
                break;
        }
    }

    // Clock tick (1MHz peripheral clock)
    void tick() {
        // Will be implemented in Phase 5 for command execution timing
    }

    // Interrupt status
    bool drq() const { return drq_; }
    bool intrq() const { return intrq_; }

    // Drive attachment
    void attach_drive(int drive_num, DiscDrive* drive) {
        if (drive_num >= 0 && drive_num < 2) {
            drives_[static_cast<size_t>(drive_num)] = drive;
        }
    }

    // External control signals (from glue logic / control register)
    void set_side(uint8_t side) { selected_side_ = side & 1; }
    void set_drive(uint8_t drive) { selected_drive_ = drive & 1; }
    void set_density(bool double_density) { double_density_ = double_density; }

    // State accessors for testing
    uint8_t selected_side() const { return selected_side_; }
    uint8_t selected_drive() const { return selected_drive_; }
    bool is_double_density() const { return double_density_; }

    // Reset controller
    void reset() {
        status_ = 0x00;
        track_ = 0x00;
        sector_ = 0x01;  // WD1770 sector register defaults to 1
        data_ = 0x00;
        command_ = 0x00;

        drq_ = false;
        intrq_ = false;

        selected_side_ = 0;
        selected_drive_ = 0;
        double_density_ = false;
    }

    // Identification
    const char* name() const { return "WD1770"; }

private:
    // Registers
    uint8_t status_ = 0x00;
    uint8_t track_ = 0x00;
    uint8_t sector_ = 0x01;  // Defaults to 1 per WD1770 spec
    uint8_t data_ = 0x00;
    uint8_t command_ = 0x00;

    // Interrupt request lines
    bool drq_ = false;    // Data request
    bool intrq_ = false;  // Interrupt request (drives NMI)

    // Drive connections
    std::array<DiscDrive*, 2> drives_{nullptr, nullptr};

    // External control signals
    uint8_t selected_side_ = 0;
    uint8_t selected_drive_ = 0;
    bool double_density_ = false;
};

} // namespace beebium
