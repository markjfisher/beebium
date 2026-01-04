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
#include <cstdio>  // For debug output
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
            case 0: {
                // Reading status clears INTRQ
                intrq_ = false;
                // Update status with current DRQ state
                uint8_t result = status_;
                if (drq_) {
                    result |= STATUS_DRQ;
                }
                return result;
            }
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

        if (byte_delay_ > 0) {
            --byte_delay_;
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
                // Type III commands - use upper nibble for dispatch
                tick_type3_command();
                break;
        }
    }

    // Interrupt status
    bool drq() const { return drq_; }
    bool intrq() const { return intrq_; }
    bool busy() const { return (status_ & STATUS_BUSY) != 0; }

    // NMI source interface (for NmiAggregator)
    // On the BBC Model B+ with 1770, both DRQ and INTRQ are OR'd together
    // to generate NMI (when enabled by the disc control register bit 6).
    // DRQ fires during data transfer (each byte), INTRQ at command completion.
    // Note: The actual BBC hardware has an NMI enable bit in the disc control
    // register which gates the combined signal before it reaches the CPU's NMI line.
    bool nmi_pending() const { return drq_ || intrq_; }

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
        selected_step_rate_ = 6000;  // Default to 6ms

        sector_buffer_.clear();
        byte_counter_ = 0;
        byte_delay_ = 0;
    }

    // Identification
    const char* name() const { return "WD1770"; }

public:
    // Debug flag - set externally to track commands
    static inline bool debug_enabled = false;

private:
    void execute_command(uint8_t cmd) {
        command_ = cmd;

        // Force Interrupt (Type IV) can be issued at any time
        // Bits 0-3 control interrupt conditions:
        //   I3 (bit 3): Immediate interrupt
        //   I2 (bit 2): Interrupt on every index pulse
        //   I1 (bit 1): Interrupt on Ready to Not-Ready transition
        //   I0 (bit 0): Interrupt on Not-Ready to Ready transition
        //   All zero: Terminate command without generating interrupt
        if ((cmd & 0xF0) == CMD_FORCE_INT) {
            // Always terminate current command
            status_ &= ~STATUS_BUSY;
            drq_ = false;

            uint8_t interrupt_flags = cmd & 0x0F;
            if (interrupt_flags == 0) {
                // 0xD0: Terminate without interrupt
                // INTRQ is not set
            } else if (interrupt_flags & 0x08) {
                // I3 set: Immediate interrupt
                intrq_ = true;
            } else {
                // I0, I1, I2: Conditional interrupts
                // I2 (index pulse) would require index tracking - set INTRQ for now
                // I0, I1 (ready transitions) are not commonly used
                // For compatibility, set INTRQ when any condition flag is set
                intrq_ = true;
            }
            return;
        }

        // Other commands can only start when not busy
        if (status_ & STATUS_BUSY) {
            return;
        }

        current_command_ = cmd;
        // Clear all status bits except BUSY when starting a new command.
        // The TRACK0 bit from a previous Type I command would otherwise
        // be interpreted as LOST_DATA in a Type II/III command.
        status_ = STATUS_BUSY;
        intrq_ = false;

        // Determine step rate from command bits 0-1
        // 6ms, 12ms, 20ms, 30ms at 1MHz
        static constexpr int step_rates[] = {6000, 12000, 20000, 30000};
        int rate_index = cmd & 0x03;
        selected_step_rate_ = step_rates[rate_index];

        // Type I commands use bits 7-5 for command type (mask 0xE0)
        // But Restore (0x0x) and Seek (0x1x) need special handling
        uint8_t cmd_type = cmd & 0xE0;

        if (cmd_type == 0x00) {
            // Could be Restore (0x0x) or Seek (0x1x)
            if ((cmd & 0xF0) == CMD_SEEK) {
                // Seek: move to track in data register
                step_delay_ = selected_step_rate_;
            } else {
                // Restore: step out until track 0
                step_delay_ = selected_step_rate_;
            }
        } else if (cmd_type == 0x20) {
            // Step: single step in last direction
            step_delay_ = selected_step_rate_;
        } else if (cmd_type == 0x40) {
            // Step-In: step toward higher tracks
            step_direction_ = 1;
            step_delay_ = selected_step_rate_;
        } else if (cmd_type == 0x60) {
            // Step-Out: step toward track 0
            step_direction_ = -1;
            step_delay_ = selected_step_rate_;
        } else if (cmd_type == 0x80) {
            // Read Sector
            start_read_sector();
        } else if (cmd_type == 0xA0) {
            // Write Sector
            start_write_sector();
        } else {
            // Type III commands need upper nibble check (not 0xE0 mask)
            // because Write Track (0xF0) would otherwise map to Read Track (0xE0)
            uint8_t cmd_nibble = cmd & 0xF0;
            if (cmd_nibble == 0xC0) {
                // Read Address
                start_read_address();
            } else if (cmd_nibble == 0xE0) {
                // Read Track
                start_read_track();
            } else {
                // Write Track (0xF0)
                start_write_track();
            }
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
            step_delay_ = selected_step_rate_;  // Use selected step rate
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
            step_delay_ = selected_step_rate_;  // Use selected step rate
        } else {
            drive->step_out();
            --track_;
            step_direction_ = -1;
            step_delay_ = selected_step_rate_;  // Use selected step rate
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
        // Assert DRQ for first byte, with initial delay for sector search time.
        // Real hardware takes ~300µs to find the sector header on the disc.
        // We use a shorter delay (64µs = 1 byte time) for first byte consistency.
        data_ = sector_buffer_[0];
        drq_ = true;
        byte_delay_ = US_PER_BYTE;  // Consistent inter-byte timing from the start
    }

    void tick_read_sector() {
        // If DRQ is still set, the host hasn't read the byte yet.
        // In a real system, we might set LOST_DATA, but for now just wait.
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

        // Set up the next byte with proper inter-byte delay.
        // At 250kbps MFM, bytes arrive every ~64µs (64 1MHz ticks).
        // This delay gives the NMI handler time to complete before
        // the next DRQ asserts and triggers another NMI.
        data_ = sector_buffer_[byte_counter_];
        drq_ = true;
        byte_delay_ = US_PER_BYTE;  // Wait before next byte can be processed
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

        // Assert DRQ for first byte with consistent inter-byte timing
        drq_ = true;
        byte_delay_ = US_PER_BYTE;
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

        // Request next byte with proper inter-byte delay
        drq_ = true;
        byte_delay_ = US_PER_BYTE;
    }

    // Type III command tick dispatcher
    void tick_type3_command() {
        uint8_t cmd_nibble = current_command_ & 0xF0;
        if (cmd_nibble == 0xC0) {
            tick_read_address();
        } else if (cmd_nibble == 0xE0) {
            tick_read_track();
        } else {
            tick_write_track();
        }
    }

    // Type III command implementations

    // Read Address: reads the ID field of the next sector
    // Returns 6 bytes: Track, Side, Sector, Size, CRC1, CRC2
    void start_read_address() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        // Build the ID field for the current sector position
        // In a real drive, this would read the next ID field encountered
        // For emulation, we use the current track and simulate sector 0
        uint8_t current_track = drive->current_track();
        uint8_t sector_size_code = 1;  // 1 = 256 bytes (standard BBC)

        // Use sector register as the "next" sector encountered
        // This simulates the head being positioned at that sector
        uint8_t current_sector = sector_;
        if (current_sector >= drive->disc()->sectors_per_track()) {
            current_sector = 0;  // Wrap to sector 0
        }

        id_field_[0] = current_track;       // Track
        id_field_[1] = selected_side_;      // Side
        id_field_[2] = current_sector;      // Sector
        id_field_[3] = sector_size_code;    // Size (1 = 256 bytes)
        id_field_[4] = 0x00;                // CRC high (dummy)
        id_field_[5] = 0x00;                // CRC low (dummy)

        id_field_index_ = 0;

        // Update sector register with the sector number from ID field
        sector_ = current_sector;

        // Assert DRQ for first byte
        data_ = id_field_[0];
        drq_ = true;
    }

    void tick_read_address() {
        if (drq_) {
            return;  // Wait for host to read current byte
        }

        ++id_field_index_;

        if (id_field_index_ >= 6) {
            // All 6 bytes transferred
            complete_command();
            return;
        }

        // Provide next byte
        data_ = id_field_[id_field_index_];
        drq_ = true;
    }

    // Read Track: reads an entire track
    // For sector-based disc images, we concatenate all sectors
    void start_read_track() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        // Build track data by reading all sectors
        size_t sectors = drive->disc()->sectors_per_track();
        size_t sector_size = drive->disc()->sector_size();
        sector_buffer_.resize(sectors * sector_size);

        std::vector<uint8_t> temp_sector(sector_size);
        for (size_t s = 0; s < sectors; ++s) {
            if (drive->read_sector(selected_side_, static_cast<uint8_t>(s), temp_sector)) {
                std::copy(temp_sector.begin(), temp_sector.end(),
                         sector_buffer_.begin() + s * sector_size);
            } else {
                // Fill with zeros if sector read fails
                std::fill(sector_buffer_.begin() + s * sector_size,
                         sector_buffer_.begin() + (s + 1) * sector_size, 0);
            }
        }

        byte_counter_ = 0;

        // Assert DRQ for first byte
        if (!sector_buffer_.empty()) {
            data_ = sector_buffer_[0];
            drq_ = true;
        } else {
            complete_command();
        }
    }

    void tick_read_track() {
        if (drq_) {
            return;  // Wait for host to read current byte
        }

        ++byte_counter_;

        if (byte_counter_ >= sector_buffer_.size()) {
            complete_command();
            return;
        }

        // Provide next byte
        data_ = sector_buffer_[byte_counter_];
        drq_ = true;
    }

    // Write Track: formats/writes an entire track
    // For sector-based disc images, we receive data and write sectors
    void start_write_track() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        if (drive->is_write_protected()) {
            status_ |= STATUS_WRITE_PROT;
            complete_command();
            return;
        }

        // Prepare buffer for track data
        // In a real WD1770, the track format would be parsed for sync bytes,
        // address marks, and data fields. For simplicity, we accept raw sector
        // data and write it to all sectors on the track.
        size_t sectors = drive->disc()->sectors_per_track();
        size_t sector_size = drive->disc()->sector_size();
        sector_buffer_.resize(sectors * sector_size);
        byte_counter_ = 0;

        // Assert DRQ for first byte
        drq_ = true;
    }

    void tick_write_track() {
        if (drq_) {
            return;  // Wait for host to write next byte
        }

        // Store the byte
        if (byte_counter_ < sector_buffer_.size()) {
            sector_buffer_[byte_counter_] = data_;
        }
        ++byte_counter_;

        if (byte_counter_ >= sector_buffer_.size()) {
            // All bytes received, write sectors to disc
            DiscDrive* drive = get_current_drive();
            if (drive && drive->has_disc()) {
                size_t sectors = drive->disc()->sectors_per_track();
                size_t sector_size = drive->disc()->sector_size();

                std::vector<uint8_t> temp_sector(sector_size);
                for (size_t s = 0; s < sectors; ++s) {
                    std::copy(sector_buffer_.begin() + s * sector_size,
                             sector_buffer_.begin() + (s + 1) * sector_size,
                             temp_sector.begin());
                    drive->write_sector(selected_side_, static_cast<uint8_t>(s), temp_sector);
                }
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
    int selected_step_rate_ = 6000;  // Selected step rate in ticks

    // Type II/III command state
    std::vector<uint8_t> sector_buffer_;
    size_t byte_counter_ = 0;
    int byte_delay_ = 0;  // Ticks until next byte during data transfer

    // Inter-byte timing: At 250kbps MFM (double density), each byte takes ~64µs.
    // This is 64 1MHz ticks. The WD1770 is clocked at 1MHz.
    static constexpr int US_PER_BYTE = 64;

    // Type III Read Address state
    // ID field: Track, Side, Sector, Size, CRC1, CRC2
    std::array<uint8_t, 6> id_field_{};
    uint8_t id_field_index_ = 0;
};

} // namespace beebium
