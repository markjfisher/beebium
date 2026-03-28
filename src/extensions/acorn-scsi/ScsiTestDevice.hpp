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

#ifndef BEEBIUM_SCSI_TEST_DEVICE_HPP
#define BEEBIUM_SCSI_TEST_DEVICE_HPP

#include "ScsiTarget.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// A recorded SCSI command for test assertions.
struct RecordedScsiCommand {
    std::vector<uint8_t> cdb;
    std::vector<uint8_t> data_out;
};

// Configurable test target that records all commands and returns
// pre-programmed responses. This is the primary tool for testing
// the SCSI bus protocol without a real disc image.
class ScsiTestDevice : public ScsiTarget {
public:
    // Configure presence
    void set_present(bool present) { present_ = present; }

    // Configure default status for all commands
    void set_default_status(uint8_t status) { default_status_ = status; }

    // Configure data returned by READ-type commands
    void set_read_data(std::vector<uint8_t> data) { read_data_ = std::move(data); }

    // Configure INQUIRY response
    void set_inquiry_response(std::vector<uint8_t> data) { inquiry_data_ = std::move(data); }

    // Configure sense data for REQUEST SENSE
    void set_sense_data(std::vector<uint8_t> data) { sense_data_ = std::move(data); }

    // Configure capacity for READ CAPACITY (block count and block size)
    void set_capacity(uint32_t block_count, uint32_t block_size = scsi::ACORN_BLOCK_SIZE);

    // Configure MODE SENSE response
    void set_mode_sense_data(std::vector<uint8_t> data) { mode_sense_data_ = std::move(data); }

    // Inspect recorded commands
    const std::vector<RecordedScsiCommand>& recorded_commands() const { return recorded_; }
    size_t command_count() const { return recorded_.size(); }
    void clear_recorded_commands() { recorded_.clear(); }

    // ScsiTarget interface
    ScsiCommandResult execute(std::span<const uint8_t> cdb,
                              std::span<const uint8_t> data_out) override;
    bool is_present() const override { return present_; }
    std::string_view device_type() const override { return "test-device"; }
    std::string_view description() const override { return description_; }

    void set_description(std::string desc) { description_ = std::move(desc); }

private:
    bool present_ = false;
    uint8_t default_status_ = scsi::STATUS_GOOD;
    std::vector<uint8_t> read_data_;
    std::vector<uint8_t> inquiry_data_;
    std::vector<uint8_t> sense_data_;
    std::vector<uint8_t> mode_sense_data_;
    std::vector<uint8_t> capacity_data_;
    std::string description_ = "SCSI test device";
    std::vector<RecordedScsiCommand> recorded_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_TEST_DEVICE_HPP
