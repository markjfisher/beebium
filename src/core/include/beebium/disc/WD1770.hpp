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

#include "DiscDrive.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace beebium {

// Western Digital WD1770 Floppy Disc Controller emulation.
//
// Register layout (offset 0-3):
//   0: Status register (read) / Command register (write)
//   1: Track register (read/write)
//   2: Sector register (read/write)
//   3: Data register (read/write)
//
// Command types:
//   Type I:   Restore, Seek, Step, Step-In, Step-Out (positioning)
//   Type II:  Read Sector, Write Sector
//   Type III: Read Address, Read Track, Write Track
//   Type IV:  Force Interrupt
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
    static constexpr uint8_t STATUS_TRACK0      = 0x04;  // Type I only
    static constexpr uint8_t STATUS_LOST_DATA   = 0x04;  // Type II/III only
    static constexpr uint8_t STATUS_CRC_ERROR   = 0x08;
    static constexpr uint8_t STATUS_SEEK_ERROR  = 0x10;  // Type I only
    static constexpr uint8_t STATUS_RNF         = 0x10;  // Type II/III: Record Not Found
    static constexpr uint8_t STATUS_SPIN_UP     = 0x20;  // Type I only
    static constexpr uint8_t STATUS_RECORD_TYPE = 0x20;  // Type II/III only
    static constexpr uint8_t STATUS_WRITE_PROT  = 0x40;
    static constexpr uint8_t STATUS_MOTOR_ON    = 0x80;

    // Command codes (upper 4 bits)
    static constexpr uint8_t CMD_RESTORE        = 0x00;
    static constexpr uint8_t CMD_SEEK           = 0x10;
    static constexpr uint8_t CMD_STEP           = 0x20;
    static constexpr uint8_t CMD_STEP_IN        = 0x40;
    static constexpr uint8_t CMD_STEP_OUT       = 0x60;
    static constexpr uint8_t CMD_READ_SECTOR    = 0x80;
    static constexpr uint8_t CMD_WRITE_SECTOR   = 0xA0;
    static constexpr uint8_t CMD_READ_ADDRESS   = 0xC0;
    static constexpr uint8_t CMD_READ_TRACK     = 0xE0;
    static constexpr uint8_t CMD_WRITE_TRACK    = 0xF0;
    static constexpr uint8_t CMD_FORCE_INT      = 0xD0;

    // Command flags
    static constexpr uint8_t FLAG_UPDATE_TRACK  = 0x10;  // Type I: update track register
    static constexpr uint8_t FLAG_MULTI_SECTOR  = 0x10;  // Type II: multiple sectors
    static constexpr uint8_t FLAG_SIDE_SELECT   = 0x08;  // Type II: side select (if E flag set)
    static constexpr uint8_t FLAG_DELAY         = 0x04;  // Type II: 15ms delay
    static constexpr uint8_t FLAG_SIDE_COMPARE  = 0x02;  // Type II: compare side

    WD1770() = default;
    ~WD1770() = default;

    // Non-copyable
    WD1770(const WD1770&) = delete;
    WD1770& operator=(const WD1770&) = delete;

    // Register access (offset is masked to 2 bits)
    uint8_t read(uint16_t offset) {
        switch (offset & 0x03) {
            case 0:
                // Reading status clears INTRQ
                intrq_ = false;
                // Update status with current DRQ state
                if (drq_) {
                    return status_ | STATUS_DRQ;
                }
                return status_;
            case 1: return track_;
            case 2: return sector_;
            case 3:
                // Reading data register clears DRQ during Type II/III
                drq_ = false;
                return data_;
            default: return 0xFF;
        }
    }

    void write(uint16_t offset, uint8_t value) {
        switch (offset & 0x03) {
            case 0:
                execute_command(value);
                break;
            case 1:
                if (!(status_ & STATUS_BUSY)) {
                    track_ = value;
                }
                break;
            case 2:
                if (!(status_ & STATUS_BUSY)) {
                    sector_ = value;
                }
                break;
            case 3:
                data_ = value;
                drq_ = false;
                break;
        }
    }

    // Clock tick (1MHz peripheral clock)
    void tick() {
        if (!(status_ & STATUS_BUSY)) {
            return;
        }

        if (step_delay_ > 0) {
            --step_delay_;
            return;
        }

        // Execute pending operation
        // Type I commands: mask with 0xE0 to ignore T flag and step rate
        // Special case: Restore (0x0x) and Seek (0x1x) both have 0xE0 mask = 0x00
        uint8_t cmd_type = current_command_ & 0xE0;
        switch (cmd_type) {
            case 0x00:  // Restore (0x0x) or Seek (0x1x)
                if ((current_command_ & 0xF0) == CMD_SEEK) {
                    tick_seek();
                } else {
                    tick_restore();
                }
                break;
            case 0x20:  // Step (0x2x/0x3x with T flag)
                tick_step();
                break;
            case 0x40:  // Step-In (0x4x/0x5x with T flag)
                tick_step_in();
                break;
            case 0x60:  // Step-Out (0x6x/0x7x with T flag)
                tick_step_out();
                break;
            case 0x80:  // Read Sector (0x8x/0x9x)
                tick_read_sector();
                break;
            case 0xA0:  // Write Sector (0xAx/0xBx)
                tick_write_sector();
                break;
            default:
                // Type III/IV - complete immediately for now
                complete_command();
                break;
        }
    }

    // Interrupt status
    bool drq() const { return drq_; }
    bool intrq() const { return intrq_; }
    bool busy() const { return (status_ & STATUS_BUSY) != 0; }

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
        current_command_ = 0x00;

        drq_ = false;
        intrq_ = false;

        selected_side_ = 0;
        selected_drive_ = 0;
        double_density_ = false;

        step_direction_ = 1;  // Default: step in
        step_delay_ = 0;

        sector_buffer_.clear();
        byte_counter_ = 0;
    }

    // Identification
    const char* name() const { return "WD1770"; }

private:
    void execute_command(uint8_t cmd) {
        command_ = cmd;

        // Force Interrupt (Type IV) can be issued at any time
        if ((cmd & 0xF0) == CMD_FORCE_INT) {
            status_ &= ~STATUS_BUSY;
            intrq_ = true;
            return;
        }

        // Other commands can only start when not busy
        if (status_ & STATUS_BUSY) {
            return;
        }

        current_command_ = cmd;
        status_ |= STATUS_BUSY;
        intrq_ = false;

        // Determine step rate from command bits 0-1
        // 6ms, 12ms, 20ms, 30ms at 1MHz
        static constexpr int step_rates[] = {6000, 12000, 20000, 30000};
        int rate_index = cmd & 0x03;

        // Type I commands use bits 7-5 for command type (mask 0xE0)
        // But Restore (0x0x) and Seek (0x1x) need special handling
        uint8_t cmd_type = cmd & 0xE0;

        if (cmd_type == 0x00) {
            // Could be Restore (0x0x) or Seek (0x1x)
            if ((cmd & 0xF0) == CMD_SEEK) {
                // Seek: move to track in data register
                step_delay_ = step_rates[rate_index];
            } else {
                // Restore: step out until track 0
                step_delay_ = step_rates[rate_index];
            }
        } else if (cmd_type == 0x20) {
            // Step: single step in last direction
            step_delay_ = step_rates[rate_index];
        } else if (cmd_type == 0x40) {
            // Step-In: step toward higher tracks
            step_direction_ = 1;
            step_delay_ = step_rates[rate_index];
        } else if (cmd_type == 0x60) {
            // Step-Out: step toward track 0
            step_direction_ = -1;
            step_delay_ = step_rates[rate_index];
        } else if (cmd_type == 0x80) {
            // Read Sector
            start_read_sector();
        } else if (cmd_type == 0xA0) {
            // Write Sector
            start_write_sector();
        } else {
            // Type III commands - complete immediately for now
            complete_command();
        }
    }

    void tick_restore() {
        DiscDrive* drive = get_current_drive();
        if (!drive) {
            complete_command();
            return;
        }

        if (drive->at_track_0()) {
            track_ = 0;
            complete_command();
            update_track0_status();
        } else {
            drive->step_out();
            step_delay_ = 6000;  // Continue stepping
        }
    }

    void tick_seek() {
        DiscDrive* drive = get_current_drive();
        if (!drive) {
            complete_command();
            return;
        }

        if (track_ == data_) {
            complete_command();
            update_track0_status();
        } else if (track_ < data_) {
            drive->step_in();
            ++track_;
            step_direction_ = 1;
            step_delay_ = 6000;
        } else {
            drive->step_out();
            --track_;
            step_direction_ = -1;
            step_delay_ = 6000;
        }
    }

    void tick_step() {
        DiscDrive* drive = get_current_drive();
        if (!drive) {
            complete_command();
            return;
        }

        if (step_direction_ > 0) {
            drive->step_in();
            if (current_command_ & FLAG_UPDATE_TRACK) {
                ++track_;
            }
        } else {
            drive->step_out();
            if (current_command_ & FLAG_UPDATE_TRACK) {
                if (track_ > 0) --track_;
            }
        }
        complete_command();
        update_track0_status();
    }

    void tick_step_in() {
        DiscDrive* drive = get_current_drive();
        if (!drive) {
            complete_command();
            return;
        }

        drive->step_in();
        if (current_command_ & FLAG_UPDATE_TRACK) {
            ++track_;
        }
        complete_command();
        update_track0_status();
    }

    void tick_step_out() {
        DiscDrive* drive = get_current_drive();
        if (!drive) {
            complete_command();
            return;
        }

        drive->step_out();
        if (current_command_ & FLAG_UPDATE_TRACK) {
            if (track_ > 0) --track_;
        }
        complete_command();
        update_track0_status();
    }

    // Type II command implementations
    void start_read_sector() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        // Check if sector is valid
        if (sector_ >= drive->disc()->sectors_per_track()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        // Read the sector into buffer
        sector_buffer_.resize(drive->disc()->sector_size());
        if (!drive->read_sector(selected_side_, sector_, sector_buffer_)) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        byte_counter_ = 0;
        // Assert DRQ for first byte
        data_ = sector_buffer_[0];
        drq_ = true;
    }

    void tick_read_sector() {
        // If DRQ is still set, the host hasn't read the byte yet
        // In a real system, we might set LOST_DATA, but for now just wait
        if (drq_) {
            return;
        }

        ++byte_counter_;

        if (byte_counter_ >= sector_buffer_.size()) {
            // Sector complete
            if (current_command_ & FLAG_MULTI_SECTOR) {
                ++sector_;
                // In multi-sector mode, continue to next sector
                // For now, just complete after one sector
            }
            complete_command();
            return;
        }

        // Provide next byte
        data_ = sector_buffer_[byte_counter_];
        drq_ = true;
    }

    void start_write_sector() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        // Check write protection
        if (drive->is_write_protected()) {
            status_ |= STATUS_WRITE_PROT;
            complete_command();
            return;
        }

        // Check if sector is valid
        if (sector_ >= drive->disc()->sectors_per_track()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        // Prepare buffer for writing
        sector_buffer_.resize(drive->disc()->sector_size());
        byte_counter_ = 0;

        // Assert DRQ for first byte
        drq_ = true;
    }

    void tick_write_sector() {
        // If DRQ is still set, the host hasn't written the byte yet
        if (drq_) {
            return;
        }

        // Store the byte that was written
        if (byte_counter_ < sector_buffer_.size()) {
            sector_buffer_[byte_counter_] = data_;
        }
        ++byte_counter_;

        if (byte_counter_ >= sector_buffer_.size()) {
            // All bytes received, write to disc
            DiscDrive* drive = get_current_drive();
            if (drive && drive->has_disc()) {
                drive->write_sector(selected_side_, sector_, sector_buffer_);
            }

            if (current_command_ & FLAG_MULTI_SECTOR) {
                ++sector_;
            }
            complete_command();
            return;
        }

        // Request next byte
        drq_ = true;
    }

    void complete_command() {
        status_ &= ~STATUS_BUSY;
        intrq_ = true;
    }

    void update_track0_status() {
        DiscDrive* drive = get_current_drive();
        if (drive && drive->at_track_0()) {
            status_ |= STATUS_TRACK0;
        } else {
            status_ &= ~STATUS_TRACK0;
        }
    }

    DiscDrive* get_current_drive() {
        return drives_[selected_drive_];
    }

    // Registers
    uint8_t status_ = 0x00;
    uint8_t track_ = 0x00;
    uint8_t sector_ = 0x01;  // Defaults to 1 per WD1770 spec
    uint8_t data_ = 0x00;
    uint8_t command_ = 0x00;
    uint8_t current_command_ = 0x00;

    // Interrupt request lines
    bool drq_ = false;    // Data request
    bool intrq_ = false;  // Interrupt request (drives NMI)

    // Drive connections
    std::array<DiscDrive*, 2> drives_{nullptr, nullptr};

    // External control signals
    uint8_t selected_side_ = 0;
    uint8_t selected_drive_ = 0;
    bool double_density_ = false;

    // Type I command state
    int step_direction_ = 1;  // +1 = in, -1 = out
    int step_delay_ = 0;      // Ticks until next step

    // Type II command state
    std::vector<uint8_t> sector_buffer_;
    size_t byte_counter_ = 0;
};

} // namespace beebium
