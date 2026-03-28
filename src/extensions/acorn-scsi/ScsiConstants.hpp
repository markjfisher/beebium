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

#ifndef BEEBIUM_SCSI_CONSTANTS_HPP
#define BEEBIUM_SCSI_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

namespace beebium::scsi {

// SCSI-1 opcodes (union of all emulator implementations)

// Group 0 (6-byte CDB)
constexpr uint8_t OP_TEST_UNIT_READY  = 0x00;
constexpr uint8_t OP_REZERO_UNIT      = 0x01;
constexpr uint8_t OP_REQUEST_SENSE    = 0x03;
constexpr uint8_t OP_FORMAT_UNIT      = 0x04;
constexpr uint8_t OP_READ_6           = 0x08;
constexpr uint8_t OP_WRITE_6          = 0x0A;
constexpr uint8_t OP_SEEK             = 0x0B;
constexpr uint8_t OP_TRANSLATE        = 0x0F;  // Acorn vendor-specific: LBA passthrough
constexpr uint8_t OP_INQUIRY          = 0x12;
constexpr uint8_t OP_MODE_SELECT_6    = 0x15;
constexpr uint8_t OP_MODE_SENSE_6     = 0x1A;
constexpr uint8_t OP_START_STOP_UNIT  = 0x1B;
constexpr uint8_t OP_SEND_DIAGNOSTIC  = 0x1D;

// Group 1 (10-byte CDB)
constexpr uint8_t OP_READ_CAPACITY    = 0x25;
constexpr uint8_t OP_READ_10          = 0x28;
constexpr uint8_t OP_WRITE_10         = 0x2A;
constexpr uint8_t OP_WRITE_AND_VERIFY = 0x2E;
constexpr uint8_t OP_VERIFY_10        = 0x2F;

// Group 6 (6-byte CDB, vendor-specific -- VP415 LaserDisc)
constexpr uint8_t OP_READ_FCODE       = 0xC8;
constexpr uint8_t OP_WRITE_FCODE      = 0xCA;

// SCSI status bytes
constexpr uint8_t STATUS_GOOD              = 0x00;
constexpr uint8_t STATUS_CHECK_CONDITION   = 0x02;
constexpr uint8_t STATUS_BUSY              = 0x08;

// SCSI sense keys
constexpr uint8_t SENSE_NO_SENSE           = 0x00;
constexpr uint8_t SENSE_RECOVERED_ERROR    = 0x01;
constexpr uint8_t SENSE_NOT_READY          = 0x02;
constexpr uint8_t SENSE_MEDIUM_ERROR       = 0x03;
constexpr uint8_t SENSE_HARDWARE_ERROR     = 0x04;
constexpr uint8_t SENSE_ILLEGAL_REQUEST    = 0x05;
constexpr uint8_t SENSE_UNIT_ATTENTION     = 0x06;

// SCSI message bytes
constexpr uint8_t MSG_COMMAND_COMPLETE     = 0x00;

// Acorn SCSI host adapter status register bits
constexpr uint8_t SR_MSG = 0x01;  // bit 0: Message phase
constexpr uint8_t SR_BSY = 0x02;  // bit 1: Busy
constexpr uint8_t SR_IRQ = 0x10;  // bit 4: Interrupt pending
constexpr uint8_t SR_REQ = 0x20;  // bit 5: Request (data ready)
constexpr uint8_t SR_IO  = 0x40;  // bit 6: Input/Output direction
constexpr uint8_t SR_CD  = 0x80;  // bit 7: Command/Data

// Acorn SCSI host adapter register offsets (relative to 0xFC40)
constexpr uint8_t REG_DATA    = 0x00;
constexpr uint8_t REG_STATUS  = 0x01;
constexpr uint8_t REG_SELECT  = 0x02;
constexpr uint8_t REG_IRQ     = 0x03;

// Acorn-specific block size (256 bytes, not standard 512)
constexpr uint16_t ACORN_BLOCK_SIZE = 256;

// Maximum SCSI targets on one bus
constexpr int MAX_TARGETS = 8;

// CDB length by opcode group
constexpr size_t cdb_length_for_opcode(uint8_t opcode) {
    uint8_t group = (opcode >> 5) & 0x07;
    switch (group) {
        case 0: return 6;   // 0x00-0x1F
        case 1: return 10;  // 0x20-0x3F
        case 2: return 10;  // 0x40-0x5F
        case 5: return 12;  // 0xA0-0xBF
        case 6: return 6;   // 0xC0-0xDF (vendor-specific, Acorn convention)
        case 7: return 6;   // 0xE0-0xFF (vendor-specific)
        default: return 6;
    }
}

}  // namespace beebium::scsi

#endif  // BEEBIUM_SCSI_CONSTANTS_HPP
