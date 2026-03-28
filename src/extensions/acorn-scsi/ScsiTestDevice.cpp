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

#include "ScsiTestDevice.hpp"

namespace beebium {

void ScsiTestDevice::set_capacity(uint32_t block_count, uint32_t block_size) {
    // READ CAPACITY returns 8 bytes: last LBA (4 bytes BE) + block size (4 bytes BE)
    capacity_data_.resize(8);
    uint32_t last_lba = (block_count > 0) ? block_count - 1 : 0;
    capacity_data_[0] = static_cast<uint8_t>((last_lba >> 24) & 0xFF);
    capacity_data_[1] = static_cast<uint8_t>((last_lba >> 16) & 0xFF);
    capacity_data_[2] = static_cast<uint8_t>((last_lba >> 8) & 0xFF);
    capacity_data_[3] = static_cast<uint8_t>(last_lba & 0xFF);
    capacity_data_[4] = static_cast<uint8_t>((block_size >> 24) & 0xFF);
    capacity_data_[5] = static_cast<uint8_t>((block_size >> 16) & 0xFF);
    capacity_data_[6] = static_cast<uint8_t>((block_size >> 8) & 0xFF);
    capacity_data_[7] = static_cast<uint8_t>(block_size & 0xFF);
}

ScsiCommandResult ScsiTestDevice::execute(std::span<const uint8_t> cdb,
                                          std::span<const uint8_t> data_out) {
    // Record the command
    recorded_.push_back({
        std::vector<uint8_t>(cdb.begin(), cdb.end()),
        std::vector<uint8_t>(data_out.begin(), data_out.end())
    });

    if (cdb.empty()) {
        return {scsi::STATUS_CHECK_CONDITION, scsi::MSG_COMMAND_COMPLETE, {}, 0};
    }

    uint8_t opcode = cdb[0];
    ScsiCommandResult result;
    result.status = default_status_;
    result.message = scsi::MSG_COMMAND_COMPLETE;

    switch (opcode) {
        case scsi::OP_TEST_UNIT_READY:
        case scsi::OP_REZERO_UNIT:
        case scsi::OP_SEEK:
        case scsi::OP_START_STOP_UNIT:
        case scsi::OP_SEND_DIAGNOSTIC:
            // No data commands
            break;

        case scsi::OP_REQUEST_SENSE: {
            if (!sense_data_.empty()) {
                result.data_in = sense_data_;
            } else {
                // Default: no sense, no error
                result.data_in.resize(4, 0);
            }
            // Trim to allocation length
            if (cdb.size() >= 5 && cdb[4] < result.data_in.size()) {
                result.data_in.resize(cdb[4]);
            }
            break;
        }

        case scsi::OP_FORMAT_UNIT:
            // No data (simplified -- real FORMAT can have defect list)
            break;

        case scsi::OP_READ_6: {
            uint8_t block_count = (cdb.size() >= 5) ? cdb[4] : 1;
            if (block_count == 0) block_count = 0;  // 0 means 256 blocks, handled below
            size_t byte_count = (block_count == 0 ? 256 : block_count) * scsi::ACORN_BLOCK_SIZE;
            if (!read_data_.empty()) {
                result.data_in = read_data_;
                result.data_in.resize(byte_count, 0);  // pad or trim
            } else {
                result.data_in.resize(byte_count, 0);
            }
            break;
        }

        case scsi::OP_WRITE_6: {
            uint8_t block_count = (cdb.size() >= 5) ? cdb[4] : 1;
            size_t byte_count = (block_count == 0 ? 256 : block_count) * scsi::ACORN_BLOCK_SIZE;
            if (data_out.empty()) {
                result.data_out_expected = byte_count;
            }
            // Data already received in data_out -- nothing to return
            break;
        }

        case scsi::OP_TRANSLATE: {
            // Acorn vendor-specific: return 4-byte LBA in little-endian
            if (cdb.size() >= 4) {
                uint32_t lba = (static_cast<uint32_t>(cdb[1] & 0x1F) << 16)
                             | (static_cast<uint32_t>(cdb[2]) << 8)
                             | static_cast<uint32_t>(cdb[3]);
                result.data_in.resize(4);
                result.data_in[0] = static_cast<uint8_t>(lba & 0xFF);
                result.data_in[1] = static_cast<uint8_t>((lba >> 8) & 0xFF);
                result.data_in[2] = static_cast<uint8_t>((lba >> 16) & 0xFF);
                result.data_in[3] = static_cast<uint8_t>((lba >> 24) & 0xFF);
            }
            break;
        }

        case scsi::OP_INQUIRY: {
            if (!inquiry_data_.empty()) {
                result.data_in = inquiry_data_;
            } else {
                // Default minimal inquiry response
                result.data_in.resize(36, 0);
                result.data_in[0] = 0x00;  // Direct access device
                result.data_in[1] = 0x00;  // Not removable
                result.data_in[2] = 0x01;  // SCSI-1
                result.data_in[4] = 31;    // Additional length
            }
            if (cdb.size() >= 5 && cdb[4] < result.data_in.size()) {
                result.data_in.resize(cdb[4]);
            }
            break;
        }

        case scsi::OP_MODE_SELECT_6: {
            size_t param_length = (cdb.size() >= 5) ? cdb[4] : 0;
            if (data_out.empty() && param_length > 0) {
                result.data_out_expected = param_length;
            }
            break;
        }

        case scsi::OP_MODE_SENSE_6: {
            if (!mode_sense_data_.empty()) {
                result.data_in = mode_sense_data_;
            } else {
                result.data_in.resize(22, 0);
            }
            if (cdb.size() >= 5 && cdb[4] < result.data_in.size()) {
                result.data_in.resize(cdb[4]);
            }
            break;
        }

        case scsi::OP_READ_CAPACITY: {
            if (!capacity_data_.empty()) {
                result.data_in = capacity_data_;
            } else {
                result.data_in.resize(8, 0);
            }
            break;
        }

        case scsi::OP_READ_10: {
            uint16_t block_count = 0;
            if (cdb.size() >= 9) {
                block_count = (static_cast<uint16_t>(cdb[7]) << 8) | cdb[8];
            }
            size_t byte_count = block_count * scsi::ACORN_BLOCK_SIZE;
            if (!read_data_.empty()) {
                result.data_in = read_data_;
                result.data_in.resize(byte_count, 0);
            } else {
                result.data_in.resize(byte_count, 0);
            }
            break;
        }

        case scsi::OP_WRITE_10:
        case scsi::OP_WRITE_AND_VERIFY: {
            uint16_t block_count = 0;
            if (cdb.size() >= 9) {
                block_count = (static_cast<uint16_t>(cdb[7]) << 8) | cdb[8];
            }
            size_t byte_count = block_count * scsi::ACORN_BLOCK_SIZE;
            if (data_out.empty() && byte_count > 0) {
                result.data_out_expected = byte_count;
            }
            break;
        }

        case scsi::OP_VERIFY_10:
            // No data transfer
            break;

        case scsi::OP_READ_FCODE: {
            // VP415 vendor-specific: return 256 bytes
            if (!read_data_.empty()) {
                result.data_in = read_data_;
                result.data_in.resize(scsi::ACORN_BLOCK_SIZE, 0);
            } else {
                result.data_in.resize(scsi::ACORN_BLOCK_SIZE, 0);
            }
            break;
        }

        case scsi::OP_WRITE_FCODE: {
            // VP415 vendor-specific: expect 256 bytes
            if (data_out.empty()) {
                result.data_out_expected = scsi::ACORN_BLOCK_SIZE;
            }
            break;
        }

        default:
            // Unknown command
            result.status = scsi::STATUS_CHECK_CONDITION;
            break;
    }

    return result;
}

}  // namespace beebium
