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

#ifndef BEEBIUM_SCSI_BUS_HPP
#define BEEBIUM_SCSI_BUS_HPP

#include "ScsiConstants.hpp"
#include "ScsiTarget.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace beebium {

// SCSI bus phases
enum class ScsiBusPhase : uint8_t {
    BusFree,
    Selection,
    Command,
    DataIn,       // Target -> Initiator (host reads data register)
    DataOut,      // Initiator -> Target (host writes data register)
    Status,
    MessageIn,
};

// Convert phase to string for debugging and gRPC.
constexpr std::string_view scsi_phase_name(ScsiBusPhase phase) {
    switch (phase) {
        case ScsiBusPhase::BusFree:   return "BUS_FREE";
        case ScsiBusPhase::Selection: return "SELECTION";
        case ScsiBusPhase::Command:   return "COMMAND";
        case ScsiBusPhase::DataIn:    return "DATA_IN";
        case ScsiBusPhase::DataOut:   return "DATA_OUT";
        case ScsiBusPhase::Status:    return "STATUS";
        case ScsiBusPhase::MessageIn: return "MESSAGE_IN";
        default:                      return "UNKNOWN";
    }
}

// SCSI bus state machine.
//
// Models the Acorn SCSI host adapter bus protocol. The 6502 interacts with
// the bus via four registers (data, status, select, IRQ control). The bus
// translates register reads/writes into complete SCSI transactions dispatched
// to targets via the ScsiTarget interface.
//
// This class has no dependency on the extension framework and is fully
// unit-testable with ScsiTestDevice.
class ScsiBus {
public:
    // Register a target at a SCSI ID (0-7). Non-owning pointer.
    // The caller (ScsiTargetRegistry) owns the target lifetime.
    void set_target(uint8_t id, ScsiTarget* target);
    ScsiTarget* target(uint8_t id) const;

    // Register interface (called by the host adapter extension).
    // reg = 0-3 (offsets within the 4-byte register window).
    uint8_t read_register(uint8_t reg);
    void write_register(uint8_t reg, uint8_t value);

    // Query state
    ScsiBusPhase phase() const { return phase_; }
    uint8_t status_register() const;
    uint8_t selected_target_id() const { return selected_id_; }

    // IRQ state
    bool irq_pending() const { return irq_enabled_ && irq_asserted_; }

    // Diagnostic trace
    void set_trace_enabled(bool enabled) { trace_enabled_ = enabled; }

private:
    // Phase transitions
    void enter_bus_free();
    void enter_selection();
    void enter_command();
    void enter_data_in(std::vector<uint8_t> data);
    void enter_data_out(size_t expected_bytes);
    void enter_status(uint8_t status_byte, uint8_t message_byte);
    void enter_message_in();

    // Register handlers
    uint8_t read_data();
    void write_data(uint8_t value);
    void write_select(uint8_t value);
    void write_irq_control(uint8_t value);

    // Command processing
    void receive_cdb_byte(uint8_t byte);
    void dispatch_command();

    // Decode transfer direction and size from a complete CDB.
    struct TransferInfo {
        enum class Direction { None, DataIn, DataOut };
        Direction direction = Direction::None;
        size_t byte_count = 0;
    };
    static TransferInfo decode_transfer(std::span<const uint8_t> cdb);

    // Bus state
    ScsiBusPhase phase_ = ScsiBusPhase::BusFree;
    std::array<ScsiTarget*, scsi::MAX_TARGETS> targets_{};
    uint8_t selected_id_ = 0xFF;

    // Selection state
    bool sel_asserted_ = false;
    uint8_t selection_data_ = 0;  // data byte written during selection
    uint8_t last_data_write_ = 0; // last value written to data register

    // CDB assembly
    std::array<uint8_t, 12> cdb_buffer_{};
    size_t cdb_expected_length_ = 0;
    size_t cdb_index_ = 0;

    // Data buffer (sized per-transfer)
    std::vector<uint8_t> data_buffer_;
    size_t data_index_ = 0;

    // Status and message
    uint8_t status_byte_ = 0;
    uint8_t message_byte_ = 0;

    // IRQ
    bool irq_enabled_ = false;
    bool irq_asserted_ = false;

    // Trace
    bool trace_enabled_ = false;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_BUS_HPP
