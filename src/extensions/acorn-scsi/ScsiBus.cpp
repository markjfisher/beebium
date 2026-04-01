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

#include <string>

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
    uint8_t result;
    switch (reg) {
        case scsi::REG_DATA:   result = read_data(); break;
        case scsi::REG_STATUS: result = status_register(); break;
        default:               result = 0xFF; break;
    }
    emit_register_access("READ", reg, result);
    return result;
}

void ScsiBus::write_register(uint8_t reg, uint8_t value) {
    emit_register_access("WRITE", reg, value);
    switch (reg) {
        case scsi::REG_DATA:
            // Writing to registers 0 or 1 asserts SEL on the bus. This
            // distinguishes data bus writes from the select register (reg 2)
            // which deasserts SEL to trigger the selection handshake.
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
        case ScsiBusPhase::BusFree:
        case ScsiBusPhase::Selection:
            // In BusFree and Selection, return the last value written to the
            // data register. This is what b2 does (m_last_write) and is needed
            // for ADFS's selection handshake which writes the target ID then
            // reads it back to confirm the bus is responding.
            return last_data_write_;

        case ScsiBusPhase::DataIn: {
            if (data_index_ < data_buffer_.size()) {
                uint8_t byte = data_buffer_[data_index_++];
                if (data_index_ >= data_buffer_.size()) {
                    emit_data_transfer("IN",
                        static_cast<uint32_t>(data_buffer_.size()),
                        static_cast<uint32_t>(data_index_), true);
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
    last_data_write_ = value;
    switch (phase_) {
        case ScsiBusPhase::BusFree:
            // Write to data register in BusFree. Save the value for read-back
            // (ADFS probe) but do NOT enter Selection. Selection is triggered
            // by the write to REG_SELECT (reg 2) which deasserts SEL.
            //
            // ADFS writes test patterns (0x5A, 0xA5) to the data register
            // during its hardware probe and reads them back. If we entered
            // Selection on these writes, ADFS would see BSY=1 and loop
            // forever. Instead, we stay in BusFree and let the probe succeed.
            // When ADFS later initiates a real SCSI transaction, it writes
            // the target ID to reg 0 then writes to reg 2 to begin selection.
            if (sel_asserted_) {
                selection_data_ = value;
            }
            break;

        case ScsiBusPhase::Selection:
            // Write to data register in Selection. If SEL is deasserted
            // (write came via write_select/reg 2), transition to Command.
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
                    emit_data_transfer("OUT",
                        static_cast<uint32_t>(data_buffer_.size()),
                        static_cast<uint32_t>(data_index_), true);
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
    // Write to select register (offset 0x02) deasserts SEL.
    // This triggers the selection handshake and transitions to Command.
    // The value written to reg 2 is NOT passed to write_data -- it is
    // purely a control signal. Passing it through would inject a spurious
    // byte into the Command phase CDB buffer.
    (void)value;
    if (phase_ == ScsiBusPhase::BusFree) {
        enter_selection();
        if (phase_ == ScsiBusPhase::Selection) {
            enter_command();
        }
    } else if (phase_ == ScsiBusPhase::Selection) {
        enter_command();
    }
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
    auto old_phase = scsi_phase_name(phase_);
    phase_ = ScsiBusPhase::BusFree;
    emit_phase_change(old_phase, scsi_phase_name(phase_));
    selected_id_ = 0xFF;
    sel_asserted_ = false;
    cdb_index_ = 0;
    cdb_expected_length_ = 0;
    data_buffer_.clear();
    data_index_ = 0;
}

void ScsiBus::enter_selection() {
    // Enter Selection phase. The target is identified from the selection_data_
    // byte written to the data register before the selection trigger. Each
    // bit corresponds to a SCSI ID (bit 0 = ID 0, bit 1 = ID 1, etc.).
    // Select the first target whose bit is set and that is present.
    for (uint8_t id = 0; id < scsi::MAX_TARGETS; ++id) {
        if ((selection_data_ & (1 << id)) && targets_[id] && targets_[id]->is_present()) {
            phase_ = ScsiBusPhase::Selection;
            selected_id_ = id;
            emit_selection(id, true);
            return;
        }
    }
    // No matching target present -- stay in BusFree
    emit_selection(0xFF, false);
}

void ScsiBus::enter_command() {
    emit_phase_change(scsi_phase_name(phase_), "COMMAND");
    phase_ = ScsiBusPhase::Command;
    cdb_index_ = 0;
    cdb_expected_length_ = 0;  // determined by first byte (opcode)
    cdb_buffer_.fill(0);
}

void ScsiBus::enter_data_in(std::vector<uint8_t> data) {
    emit_phase_change(scsi_phase_name(phase_), "DATA_IN");
    emit_data_transfer("IN", static_cast<uint32_t>(data.size()), 0, false);
    phase_ = ScsiBusPhase::DataIn;
    data_buffer_ = std::move(data);
    data_index_ = 0;
}

void ScsiBus::enter_data_out(size_t expected_bytes) {
    emit_phase_change(scsi_phase_name(phase_), "DATA_OUT");
    emit_data_transfer("OUT", static_cast<uint32_t>(expected_bytes), 0, false);
    phase_ = ScsiBusPhase::DataOut;
    data_buffer_.resize(expected_bytes, 0);
    data_index_ = 0;
}

void ScsiBus::enter_status(uint8_t status, uint8_t message) {
    emit_phase_change(scsi_phase_name(phase_), "STATUS");
    emit_status(selected_id_, status, message);
    phase_ = ScsiBusPhase::Status;
    status_byte_ = status;
    message_byte_ = message;
    // Assert IRQ to notify the host that a phase change occurred.
    // ADFS uses IRQ-driven I/O for SCSI operations: after sending a
    // command or data, it enables IRQ and waits for the interrupt handler
    // to signal completion. Without this, ADFS hangs after DATA_OUT
    // transfers because it never learns the bus has reached STATUS phase.
    if (irq_enabled_) {
        irq_asserted_ = true;
    }
}

void ScsiBus::enter_message_in() {
    emit_phase_change(scsi_phase_name(phase_), "MESSAGE_IN");
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
        emit_command(selected_id_, cdb);
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

// ---------------------------------------------------------------------------
// Event emission helpers
// ---------------------------------------------------------------------------

static std::string scsi_opcode_name(uint8_t opcode) {
    switch (opcode) {
        case scsi::OP_TEST_UNIT_READY:  return "TEST UNIT READY";
        case scsi::OP_REZERO_UNIT:      return "REZERO UNIT";
        case scsi::OP_REQUEST_SENSE:    return "REQUEST SENSE";
        case scsi::OP_FORMAT_UNIT:      return "FORMAT UNIT";
        case scsi::OP_READ_6:           return "READ(6)";
        case scsi::OP_WRITE_6:          return "WRITE(6)";
        case scsi::OP_SEEK:             return "SEEK";
        case scsi::OP_TRANSLATE:        return "TRANSLATE";
        case scsi::OP_INQUIRY:          return "INQUIRY";
        case scsi::OP_MODE_SELECT_6:    return "MODE SELECT(6)";
        case scsi::OP_MODE_SENSE_6:     return "MODE SENSE(6)";
        case scsi::OP_START_STOP_UNIT:  return "START/STOP UNIT";
        case scsi::OP_SEND_DIAGNOSTIC:  return "SEND DIAGNOSTIC";
        case scsi::OP_READ_CAPACITY:    return "READ CAPACITY";
        case scsi::OP_READ_10:          return "READ(10)";
        case scsi::OP_WRITE_10:         return "WRITE(10)";
        case scsi::OP_WRITE_AND_VERIFY: return "WRITE AND VERIFY";
        case scsi::OP_VERIFY_10:        return "VERIFY(10)";
        case scsi::OP_READ_FCODE:       return "READ F-CODE";
        case scsi::OP_WRITE_FCODE:      return "WRITE F-CODE";
        default:                        return "UNKNOWN(0x" + std::to_string(opcode) + ")";
    }
}

static std::string scsi_status_name(uint8_t status) {
    switch (status) {
        case scsi::STATUS_GOOD:            return "GOOD";
        case scsi::STATUS_CHECK_CONDITION: return "CHECK CONDITION";
        case scsi::STATUS_BUSY:            return "BUSY";
        default:                           return "UNKNOWN";
    }
}

static constexpr const char* register_name(uint8_t reg) {
    switch (reg) {
        case scsi::REG_DATA:   return "DATA";
        case scsi::REG_STATUS: return "STATUS";
        case scsi::REG_SELECT: return "SELECT";
        case scsi::REG_IRQ:    return "IRQ";
        default:               return "?";
    }
}

void ScsiBus::emit_phase_change(std::string_view from, std::string_view to) {
    if (!event_buffer_) return;
    event_buffer_->push(ScsiPhaseChangeEvent{std::string(from), std::string(to)});
}

void ScsiBus::emit_selection(uint8_t id, bool success) {
    if (!event_buffer_) return;
    event_buffer_->push(ScsiSelectionEvent{id, success});
}

void ScsiBus::emit_command(uint8_t target_id, std::span<const uint8_t> cdb) {
    if (!event_buffer_) return;
    ScsiCommandEvent e;
    e.target_id = target_id;
    e.opcode = cdb.empty() ? 0 : cdb[0];
    e.opcode_name = scsi_opcode_name(e.opcode);
    e.cdb.assign(cdb.begin(), cdb.end());

    // Decode LBA and block count from CDB
    if (cdb.size() >= 6 && (e.opcode == scsi::OP_READ_6 || e.opcode == scsi::OP_WRITE_6)) {
        e.lba = (static_cast<uint32_t>(cdb[1] & 0x1F) << 16)
              | (static_cast<uint32_t>(cdb[2]) << 8)
              | static_cast<uint32_t>(cdb[3]);
        e.block_count = cdb[4] == 0 ? 256 : cdb[4];
    } else if (cdb.size() >= 10 && (e.opcode == scsi::OP_READ_10 || e.opcode == scsi::OP_WRITE_10
               || e.opcode == scsi::OP_WRITE_AND_VERIFY)) {
        e.lba = (static_cast<uint32_t>(cdb[2]) << 24)
              | (static_cast<uint32_t>(cdb[3]) << 16)
              | (static_cast<uint32_t>(cdb[4]) << 8)
              | static_cast<uint32_t>(cdb[5]);
        e.block_count = (static_cast<uint32_t>(cdb[7]) << 8) | cdb[8];
    }

    event_buffer_->push(std::move(e));
}

void ScsiBus::emit_data_transfer(std::string_view direction, uint32_t expected,
                                  uint32_t transferred, bool complete) {
    if (!event_buffer_) return;
    event_buffer_->push(ScsiDataTransferEvent{
        std::string(direction), expected, transferred, complete});
}

void ScsiBus::emit_status(uint8_t target_id, uint8_t status, uint8_t message) {
    if (!event_buffer_) return;
    event_buffer_->push(ScsiStatusEvent{
        target_id, status, scsi_status_name(status), message});
}

void ScsiBus::emit_register_access(std::string_view op, uint8_t reg, uint8_t value) {
    if (!event_buffer_ || !event_register_access_) return;
    event_buffer_->push(ScsiRegisterAccessEvent{
        std::string(op), reg, register_name(reg), value,
        std::string(scsi_phase_name(phase_))});
}

}  // namespace beebium
