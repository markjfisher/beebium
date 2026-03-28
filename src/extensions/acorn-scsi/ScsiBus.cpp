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

#include "ScsiBus.hpp"

#include <algorithm>

namespace beebium {

// ---------------------------------------------------------------------------
// Target management
// ---------------------------------------------------------------------------

void ScsiBus::set_target(uint8_t id, ScsiTarget* target) {
    if (id < scsi::MAX_TARGETS) {
        targets_[id] = target;
    }
}

ScsiTarget* ScsiBus::target(uint8_t id) const {
    if (id < scsi::MAX_TARGETS) {
        return targets_[id];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Register interface
// ---------------------------------------------------------------------------

uint8_t ScsiBus::read_register(uint8_t reg) {
    switch (reg) {
        case scsi::REG_DATA:   return read_data();
        case scsi::REG_STATUS: return status_register();
        default:               return 0xFF;
    }
}

void ScsiBus::write_register(uint8_t reg, uint8_t value) {
    switch (reg) {
        case scsi::REG_DATA:
            sel_asserted_ = true;
            write_data(value);
            break;
        case scsi::REG_STATUS:
            sel_asserted_ = true;
            break;
        case scsi::REG_SELECT:
            sel_asserted_ = false;
            write_select(value);
            break;
        case scsi::REG_IRQ:
            write_irq_control(value);
            break;
    }
}

// ---------------------------------------------------------------------------
// Status register
// ---------------------------------------------------------------------------

uint8_t ScsiBus::status_register() const {
    // REQ is always set (0x20) -- this matches the Acorn SCSI host adapter
    // hardware behaviour and is required by the ADFS SCSI driver. Both b2
    // and BeebEm hardcode REQ=1 in all phases. BeebEm comments: "don't know
    // why req has to always be active? If start at 0x00, ADFS lock up on entry".
    uint8_t sr = scsi::SR_REQ;

    switch (phase_) {
        case ScsiBusPhase::BusFree:
            // Only REQ set, all others clear
            break;

        case ScsiBusPhase::Selection:
            sr |= scsi::SR_BSY;
            break;

        case ScsiBusPhase::Command:
            // CD=1 IO=0 : host writes command bytes
            sr |= scsi::SR_BSY | scsi::SR_CD;
            break;

        case ScsiBusPhase::DataIn:
            // CD=0 IO=1 : host reads data
            sr |= scsi::SR_BSY | scsi::SR_IO;
            break;

        case ScsiBusPhase::DataOut:
            // CD=0 IO=0 : host writes data
            sr |= scsi::SR_BSY;
            break;

        case ScsiBusPhase::Status:
            // CD=1 IO=1 : host reads status byte
            sr |= scsi::SR_BSY | scsi::SR_CD | scsi::SR_IO;
            break;

        case ScsiBusPhase::MessageIn:
            // CD=1 IO=1 MSG=1 : host reads message byte
            sr |= scsi::SR_BSY | scsi::SR_CD | scsi::SR_IO | scsi::SR_MSG;
            break;
    }

    if (irq_asserted_) {
        sr |= scsi::SR_IRQ;
    }

    return sr;
}

// ---------------------------------------------------------------------------
// Data register read
// ---------------------------------------------------------------------------

uint8_t ScsiBus::read_data() {
    switch (phase_) {
        case ScsiBusPhase::DataIn: {
            if (data_index_ < data_buffer_.size()) {
                uint8_t byte = data_buffer_[data_index_++];
                if (data_index_ >= data_buffer_.size()) {
                    enter_status(status_byte_, message_byte_);
                }
                return byte;
            }
            return 0xFF;
        }

        case ScsiBusPhase::Status: {
            uint8_t status = status_byte_;
            enter_message_in();
            return status;
        }

        case ScsiBusPhase::MessageIn: {
            uint8_t message = message_byte_;
            enter_bus_free();
            return message;
        }

        default:
            return 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Data register write
// ---------------------------------------------------------------------------

void ScsiBus::write_data(uint8_t value) {
    switch (phase_) {
        case ScsiBusPhase::BusFree:
            // Write to data register in BusFree with sel_asserted triggers selection.
            // The data value is saved for later target ID resolution but does NOT
            // determine whether selection occurs -- matching b2/BeebEm behaviour
            // where ADFS writes the target bitmask and the emulator just enters
            // Selection unconditionally.
            if (sel_asserted_) {
                selection_data_ = value;
                enter_selection();
            }
            break;

        case ScsiBusPhase::Selection:
            // Write to data register in Selection with !sel (i.e. via Write2
            // which calls WriteData after clearing sel) transitions to Command.
            // This matches b2's protocol where Write2 deasserts SEL and also
            // passes the value through WriteData.
            if (!sel_asserted_) {
                enter_command();
            }
            break;

        case ScsiBusPhase::Command:
            receive_cdb_byte(value);
            break;

        case ScsiBusPhase::DataOut:
            if (data_index_ < data_buffer_.size()) {
                data_buffer_[data_index_++] = value;
                if (data_index_ >= data_buffer_.size()) {
                    dispatch_command();
                }
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Select register write
// ---------------------------------------------------------------------------

void ScsiBus::write_select(uint8_t value) {
    // Write to select register (offset 0x02) deasserts SEL then passes the
    // value through write_data -- matching b2's Write2 which does both.
    // In Selection phase, write_data sees !sel and transitions to Command.
    write_data(value);
}

// ---------------------------------------------------------------------------
// IRQ control
// ---------------------------------------------------------------------------

void ScsiBus::write_irq_control(uint8_t value) {
    if (value == 0xFF) {
        irq_enabled_ = true;
        irq_asserted_ = true;
    } else {
        irq_enabled_ = false;
        irq_asserted_ = false;
    }
}

// ---------------------------------------------------------------------------
// Phase transitions
// ---------------------------------------------------------------------------

void ScsiBus::enter_bus_free() {
    phase_ = ScsiBusPhase::BusFree;
    selected_id_ = 0xFF;
    sel_asserted_ = false;
    cdb_index_ = 0;
    cdb_expected_length_ = 0;
    data_buffer_.clear();
    data_index_ = 0;
}

void ScsiBus::enter_selection() {
    // Enter Selection phase. The target ID is resolved later from the CDB's
    // LUN field (bits 5-7 of byte 1). For now we select target 0 if present,
    // matching the single-initiator single-target model used by Acorn ADFS.
    // The selection_data_ byte written by the host is available but not used
    // for target resolution by b2 or BeebEm.
    uint8_t id = 0;
    for (uint8_t i = 0; i < scsi::MAX_TARGETS; ++i) {
        if (selection_data_ & (1 << i)) {
            id = i;
            break;
        }
    }

    if (id >= scsi::MAX_TARGETS || !targets_[id] || !targets_[id]->is_present()) {
        // No target -- stay in BusFree
        return;
    }

    phase_ = ScsiBusPhase::Selection;
    selected_id_ = id;
}

void ScsiBus::enter_command() {
    phase_ = ScsiBusPhase::Command;
    cdb_index_ = 0;
    cdb_expected_length_ = 0;  // determined by first byte (opcode)
    cdb_buffer_.fill(0);
}

void ScsiBus::enter_data_in(std::vector<uint8_t> data) {
    phase_ = ScsiBusPhase::DataIn;
    data_buffer_ = std::move(data);
    data_index_ = 0;
}

void ScsiBus::enter_data_out(size_t expected_bytes) {
    phase_ = ScsiBusPhase::DataOut;
    data_buffer_.resize(expected_bytes, 0);
    data_index_ = 0;
}

void ScsiBus::enter_status(uint8_t status, uint8_t message) {
    phase_ = ScsiBusPhase::Status;
    status_byte_ = status;
    message_byte_ = message;
}

void ScsiBus::enter_message_in() {
    phase_ = ScsiBusPhase::MessageIn;
}

// ---------------------------------------------------------------------------
// CDB assembly
// ---------------------------------------------------------------------------

void ScsiBus::receive_cdb_byte(uint8_t byte) {
    if (cdb_index_ == 0) {
        // First byte is the opcode -- determines CDB length
        cdb_expected_length_ = scsi::cdb_length_for_opcode(byte);
    }

    if (cdb_index_ < cdb_buffer_.size()) {
        cdb_buffer_[cdb_index_++] = byte;
    }

    if (cdb_index_ >= cdb_expected_length_) {
        // CDB complete -- determine what to do
        auto cdb = std::span<const uint8_t>(cdb_buffer_.data(), cdb_expected_length_);
        auto transfer = decode_transfer(cdb);

        switch (transfer.direction) {
            case TransferInfo::Direction::DataIn: {
                // Execute immediately, then deliver data to host
                auto* tgt = targets_[selected_id_];
                auto result = tgt->execute(cdb, {});
                status_byte_ = result.status;
                message_byte_ = result.message;

                if (result.status != scsi::STATUS_GOOD || result.data_in.empty()) {
                    enter_status(result.status, result.message);
                } else {
                    enter_data_in(std::move(result.data_in));
                }
                break;
            }

            case TransferInfo::Direction::DataOut: {
                // Need to collect data from host first
                enter_data_out(transfer.byte_count);
                break;
            }

            case TransferInfo::Direction::None: {
                // No data transfer -- execute immediately
                auto* tgt = targets_[selected_id_];
                auto result = tgt->execute(cdb, {});
                enter_status(result.status, result.message);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Command dispatch (called after DATA_OUT completes)
// ---------------------------------------------------------------------------

void ScsiBus::dispatch_command() {
    auto cdb = std::span<const uint8_t>(cdb_buffer_.data(), cdb_expected_length_);
    auto data_out = std::span<const uint8_t>(data_buffer_.data(), data_buffer_.size());

    auto* tgt = targets_[selected_id_];
    auto result = tgt->execute(cdb, data_out);
    enter_status(result.status, result.message);
}

// ---------------------------------------------------------------------------
// Transfer decode
// ---------------------------------------------------------------------------

ScsiBus::TransferInfo ScsiBus::decode_transfer(std::span<const uint8_t> cdb) {
    if (cdb.empty()) {
        return {TransferInfo::Direction::None, 0};
    }

    uint8_t opcode = cdb[0];

    switch (opcode) {
        // No data transfer
        case scsi::OP_TEST_UNIT_READY:
        case scsi::OP_REZERO_UNIT:
        case scsi::OP_FORMAT_UNIT:
        case scsi::OP_SEEK:
        case scsi::OP_START_STOP_UNIT:
        case scsi::OP_SEND_DIAGNOSTIC:
        case scsi::OP_VERIFY_10:
            return {TransferInfo::Direction::None, 0};

        // DATA_IN: allocation length from CDB[4]
        case scsi::OP_REQUEST_SENSE:
        case scsi::OP_INQUIRY:
        case scsi::OP_MODE_SENSE_6:
            return {TransferInfo::Direction::DataIn,
                    (cdb.size() >= 5) ? cdb[4] : 0u};

        // DATA_IN: Acorn vendor-specific TRANSLATE returns 4 bytes
        case scsi::OP_TRANSLATE:
            return {TransferInfo::Direction::DataIn, 4};

        // DATA_IN: READ CAPACITY returns 8 bytes
        case scsi::OP_READ_CAPACITY:
            return {TransferInfo::Direction::DataIn, 8};

        // DATA_IN: READ(6) -- block count from CDB[4], 0 means 256
        case scsi::OP_READ_6: {
            uint8_t blocks = (cdb.size() >= 5) ? cdb[4] : 1;
            size_t count = (blocks == 0) ? 256 : blocks;
            return {TransferInfo::Direction::DataIn, count * scsi::ACORN_BLOCK_SIZE};
        }

        // DATA_IN: READ(10) -- block count from CDB[7-8]
        case scsi::OP_READ_10: {
            uint16_t blocks = 0;
            if (cdb.size() >= 9) {
                blocks = (static_cast<uint16_t>(cdb[7]) << 8) | cdb[8];
            }
            return {TransferInfo::Direction::DataIn,
                    static_cast<size_t>(blocks) * scsi::ACORN_BLOCK_SIZE};
        }

        // DATA_OUT: WRITE(6) -- block count from CDB[4], 0 means 256
        case scsi::OP_WRITE_6: {
            uint8_t blocks = (cdb.size() >= 5) ? cdb[4] : 1;
            size_t count = (blocks == 0) ? 256 : blocks;
            return {TransferInfo::Direction::DataOut, count * scsi::ACORN_BLOCK_SIZE};
        }

        // DATA_OUT: WRITE(10), WRITE AND VERIFY -- block count from CDB[7-8]
        case scsi::OP_WRITE_10:
        case scsi::OP_WRITE_AND_VERIFY: {
            uint16_t blocks = 0;
            if (cdb.size() >= 9) {
                blocks = (static_cast<uint16_t>(cdb[7]) << 8) | cdb[8];
            }
            return {TransferInfo::Direction::DataOut,
                    static_cast<size_t>(blocks) * scsi::ACORN_BLOCK_SIZE};
        }

        // DATA_OUT: MODE SELECT(6) -- parameter list length from CDB[4]
        case scsi::OP_MODE_SELECT_6:
            return {TransferInfo::Direction::DataOut,
                    (cdb.size() >= 5) ? cdb[4] : 0u};

        // DATA_IN: VP415 READ F-CODE -- 256 bytes
        case scsi::OP_READ_FCODE:
            return {TransferInfo::Direction::DataIn, scsi::ACORN_BLOCK_SIZE};

        // DATA_OUT: VP415 WRITE F-CODE -- 256 bytes
        case scsi::OP_WRITE_FCODE:
            return {TransferInfo::Direction::DataOut, scsi::ACORN_BLOCK_SIZE};

        default:
            // Unknown opcode -- treat as no data
            return {TransferInfo::Direction::None, 0};
    }
}

}  // namespace beebium
