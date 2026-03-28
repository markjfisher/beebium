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

#include "ScsiHardDisc.hpp"

#include <algorithm>
#include <cstring>

namespace beebium {

ScsiHardDisc::ScsiHardDisc(std::unique_ptr<HardDiskImage> image)
    : image_(std::move(image)) {
    description_ = "SCSI hard disc";
    if (image_) {
        description_ += " (" + image_->dat_filepath().filename().string() + ")";
    }
    // Initial sense: no error
    set_sense(scsi::SENSE_NO_SENSE);
}

ScsiCommandResult ScsiHardDisc::execute(std::span<const uint8_t> cdb,
                                        std::span<const uint8_t> data_out) {
    if (cdb.empty() || !image_) {
        set_sense(scsi::SENSE_HARDWARE_ERROR);
        return check_condition();
    }

    uint8_t opcode = cdb[0];

    switch (opcode) {
        case scsi::OP_TEST_UNIT_READY:   return handle_test_unit_ready();
        case scsi::OP_REZERO_UNIT:       return good();
        case scsi::OP_REQUEST_SENSE:     return handle_request_sense(cdb);
        case scsi::OP_FORMAT_UNIT:       return handle_format_unit();
        case scsi::OP_READ_6:            return handle_read_6(cdb);
        case scsi::OP_WRITE_6:           return handle_write_6(cdb, data_out);
        case scsi::OP_SEEK:              return good();
        case scsi::OP_TRANSLATE:         return handle_translate(cdb);
        case scsi::OP_INQUIRY:           return handle_inquiry(cdb);
        case scsi::OP_MODE_SELECT_6:     return handle_mode_select_6(cdb, data_out);
        case scsi::OP_MODE_SENSE_6:      return handle_mode_sense_6(cdb);
        case scsi::OP_START_STOP_UNIT:   return good();
        case scsi::OP_SEND_DIAGNOSTIC:   return good();
        case scsi::OP_READ_CAPACITY:     return handle_read_capacity();
        case scsi::OP_READ_10:           return handle_read_10(cdb);
        case scsi::OP_WRITE_10:          return handle_write_10(cdb, data_out);
        case scsi::OP_WRITE_AND_VERIFY:  return handle_write_10(cdb, data_out);
        case scsi::OP_VERIFY_10:         return handle_verify_10(cdb);
        default:
            set_sense(scsi::SENSE_ILLEGAL_REQUEST, 0x20);  // Invalid command opcode
            return check_condition();
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

ScsiCommandResult ScsiHardDisc::handle_test_unit_ready() {
    return good();
}

ScsiCommandResult ScsiHardDisc::handle_request_sense(std::span<const uint8_t> cdb) {
    uint8_t alloc_length = (cdb.size() >= 5) ? cdb[4] : 4;
    ScsiCommandResult result;
    result.status = scsi::STATUS_GOOD;
    result.data_in = sense_data_;
    if (result.data_in.size() > alloc_length) {
        result.data_in.resize(alloc_length);
    }
    // Clear sense after reading (per SCSI spec)
    set_sense(scsi::SENSE_NO_SENSE);
    return result;
}

ScsiCommandResult ScsiHardDisc::handle_format_unit() {
    if (image_->is_write_protected()) {
        set_sense(scsi::SENSE_NOT_READY, 0x27);  // Write protected
        return check_condition();
    }
    image_->format();
    return good();
}

ScsiCommandResult ScsiHardDisc::handle_read_6(std::span<const uint8_t> cdb) {
    if (cdb.size() < 6) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST);
        return check_condition();
    }
    uint32_t lba = (static_cast<uint32_t>(cdb[1] & 0x1F) << 16)
                 | (static_cast<uint32_t>(cdb[2]) << 8)
                 | static_cast<uint32_t>(cdb[3]);
    uint32_t count = cdb[4];
    if (count == 0) count = 256;
    return read_sectors(lba, count);
}

ScsiCommandResult ScsiHardDisc::handle_write_6(std::span<const uint8_t> cdb,
                                                std::span<const uint8_t> data_out) {
    if (cdb.size() < 6) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST);
        return check_condition();
    }
    uint32_t lba = (static_cast<uint32_t>(cdb[1] & 0x1F) << 16)
                 | (static_cast<uint32_t>(cdb[2]) << 8)
                 | static_cast<uint32_t>(cdb[3]);
    uint32_t count = cdb[4];
    if (count == 0) count = 256;

    if (data_out.empty()) {
        ScsiCommandResult result;
        result.data_out_expected = count * scsi::ACORN_BLOCK_SIZE;
        return result;
    }
    return write_sectors(lba, count, data_out);
}

ScsiCommandResult ScsiHardDisc::handle_translate(std::span<const uint8_t> cdb) {
    if (cdb.size() < 4) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST);
        return check_condition();
    }
    uint32_t lba = (static_cast<uint32_t>(cdb[1] & 0x1F) << 16)
                 | (static_cast<uint32_t>(cdb[2]) << 8)
                 | static_cast<uint32_t>(cdb[3]);
    ScsiCommandResult result;
    result.data_in.resize(4);
    result.data_in[0] = static_cast<uint8_t>(lba & 0xFF);
    result.data_in[1] = static_cast<uint8_t>((lba >> 8) & 0xFF);
    result.data_in[2] = static_cast<uint8_t>((lba >> 16) & 0xFF);
    result.data_in[3] = static_cast<uint8_t>((lba >> 24) & 0xFF);
    return result;
}

ScsiCommandResult ScsiHardDisc::handle_inquiry(std::span<const uint8_t> cdb) {
    uint8_t alloc_length = (cdb.size() >= 5) ? cdb[4] : 36;

    std::vector<uint8_t> inquiry(36, 0);
    inquiry[0] = 0x00;   // Direct access device
    inquiry[1] = 0x00;   // Not removable
    inquiry[2] = 0x01;   // SCSI-1 compliance
    inquiry[3] = 0x00;   // Response data format
    inquiry[4] = 31;     // Additional length

    // Vendor identification (bytes 8-15): "ACORN   "
    const char* vendor = "ACORN   ";
    std::memcpy(&inquiry[8], vendor, 8);

    // Product identification (bytes 16-31): "BEEBIUM HDD     "
    const char* product = "BEEBIUM HDD     ";
    std::memcpy(&inquiry[16], product, 16);

    // Product revision (bytes 32-35): "1.0 "
    const char* revision = "1.0 ";
    std::memcpy(&inquiry[32], revision, 4);

    ScsiCommandResult result;
    result.data_in = std::move(inquiry);
    if (result.data_in.size() > alloc_length) {
        result.data_in.resize(alloc_length);
    }
    return result;
}

ScsiCommandResult ScsiHardDisc::handle_mode_select_6(std::span<const uint8_t> cdb,
                                                      std::span<const uint8_t> data_out) {
    uint8_t param_length = (cdb.size() >= 5) ? cdb[4] : 0;

    if (data_out.empty() && param_length > 0) {
        ScsiCommandResult result;
        result.data_out_expected = param_length;
        return result;
    }

    if (!data_out.empty()) {
        image_->set_device_parameters(data_out);
    }
    return good();
}

ScsiCommandResult ScsiHardDisc::handle_mode_sense_6(std::span<const uint8_t> cdb) {
    uint8_t alloc_length = (cdb.size() >= 5) ? cdb[4] : 22;

    auto params = image_->device_parameters();
    ScsiCommandResult result;
    result.data_in.assign(params.begin(), params.end());
    if (result.data_in.size() > alloc_length) {
        result.data_in.resize(alloc_length);
    }
    return result;
}

ScsiCommandResult ScsiHardDisc::handle_read_capacity() {
    uint32_t last_lba = image_->total_sectors() > 0 ? image_->total_sectors() - 1 : 0;
    uint32_t block_size = scsi::ACORN_BLOCK_SIZE;

    ScsiCommandResult result;
    result.data_in.resize(8);
    result.data_in[0] = static_cast<uint8_t>((last_lba >> 24) & 0xFF);
    result.data_in[1] = static_cast<uint8_t>((last_lba >> 16) & 0xFF);
    result.data_in[2] = static_cast<uint8_t>((last_lba >> 8) & 0xFF);
    result.data_in[3] = static_cast<uint8_t>(last_lba & 0xFF);
    result.data_in[4] = static_cast<uint8_t>((block_size >> 24) & 0xFF);
    result.data_in[5] = static_cast<uint8_t>((block_size >> 16) & 0xFF);
    result.data_in[6] = static_cast<uint8_t>((block_size >> 8) & 0xFF);
    result.data_in[7] = static_cast<uint8_t>(block_size & 0xFF);
    return result;
}

ScsiCommandResult ScsiHardDisc::handle_read_10(std::span<const uint8_t> cdb) {
    if (cdb.size() < 10) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST);
        return check_condition();
    }
    uint32_t lba = (static_cast<uint32_t>(cdb[2]) << 24)
                 | (static_cast<uint32_t>(cdb[3]) << 16)
                 | (static_cast<uint32_t>(cdb[4]) << 8)
                 | static_cast<uint32_t>(cdb[5]);
    uint32_t count = (static_cast<uint32_t>(cdb[7]) << 8) | cdb[8];
    return read_sectors(lba, count);
}

ScsiCommandResult ScsiHardDisc::handle_write_10(std::span<const uint8_t> cdb,
                                                 std::span<const uint8_t> data_out) {
    if (cdb.size() < 10) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST);
        return check_condition();
    }
    uint32_t lba = (static_cast<uint32_t>(cdb[2]) << 24)
                 | (static_cast<uint32_t>(cdb[3]) << 16)
                 | (static_cast<uint32_t>(cdb[4]) << 8)
                 | static_cast<uint32_t>(cdb[5]);
    uint32_t count = (static_cast<uint32_t>(cdb[7]) << 8) | cdb[8];

    if (data_out.empty() && count > 0) {
        ScsiCommandResult result;
        result.data_out_expected = count * scsi::ACORN_BLOCK_SIZE;
        return result;
    }
    return write_sectors(lba, count, data_out);
}

ScsiCommandResult ScsiHardDisc::handle_verify_10(std::span<const uint8_t> cdb) {
    if (cdb.size() < 10) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST);
        return check_condition();
    }
    uint32_t lba = (static_cast<uint32_t>(cdb[2]) << 24)
                 | (static_cast<uint32_t>(cdb[3]) << 16)
                 | (static_cast<uint32_t>(cdb[4]) << 8)
                 | static_cast<uint32_t>(cdb[5]);
    uint32_t count = (static_cast<uint32_t>(cdb[7]) << 8) | cdb[8];

    // Verify that the LBA range is valid
    if (count > 0 && !image_->is_valid_lba(lba + count - 1)) {
        set_sense(scsi::SENSE_ILLEGAL_REQUEST, 0x21);  // LBA out of range
        return check_condition();
    }
    return good();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ScsiCommandResult ScsiHardDisc::read_sectors(uint32_t lba, uint32_t count) {
    ScsiCommandResult result;
    result.data_in.resize(count * scsi::ACORN_BLOCK_SIZE);

    for (uint32_t i = 0; i < count; ++i) {
        if (!image_->read_sector(result.data_in.data() + i * scsi::ACORN_BLOCK_SIZE,
                                  lba + i)) {
            set_sense(scsi::SENSE_ILLEGAL_REQUEST, 0x21, 0, lba + i);
            return check_condition();
        }
    }
    return result;
}

ScsiCommandResult ScsiHardDisc::write_sectors(uint32_t lba, uint32_t count,
                                               std::span<const uint8_t> data) {
    if (image_->is_write_protected()) {
        set_sense(scsi::SENSE_NOT_READY, 0x27);
        return check_condition();
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (!image_->write_sector(data.data() + i * scsi::ACORN_BLOCK_SIZE,
                                   lba + i)) {
            set_sense(scsi::SENSE_MEDIUM_ERROR, 0x03, 0, lba + i);
            return check_condition();
        }
    }
    return good();
}

void ScsiHardDisc::set_sense(uint8_t key, uint8_t asc, uint8_t ascq, uint32_t info) {
    sense_data_.resize(18, 0);
    sense_data_[0] = 0x70;                          // Current errors, fixed format
    sense_data_[2] = key;                            // Sense key
    sense_data_[3] = static_cast<uint8_t>((info >> 24) & 0xFF);
    sense_data_[4] = static_cast<uint8_t>((info >> 16) & 0xFF);
    sense_data_[5] = static_cast<uint8_t>((info >> 8) & 0xFF);
    sense_data_[6] = static_cast<uint8_t>(info & 0xFF);
    sense_data_[7] = 10;                             // Additional sense length
    sense_data_[12] = asc;                           // Additional sense code
    sense_data_[13] = ascq;                          // Additional sense code qualifier
}

ScsiCommandResult ScsiHardDisc::check_condition() {
    return {scsi::STATUS_CHECK_CONDITION, scsi::MSG_COMMAND_COMPLETE, {}, 0};
}

ScsiCommandResult ScsiHardDisc::good() {
    return {scsi::STATUS_GOOD, scsi::MSG_COMMAND_COMPLETE, {}, 0};
}

}  // namespace beebium
