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

#ifndef BEEBIUM_SCSI_TARGET_HPP
#define BEEBIUM_SCSI_TARGET_HPP

#include "ScsiConstants.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

// Result of executing a SCSI command on a target.
struct ScsiCommandResult {
    // SCSI status byte (STATUS_GOOD, STATUS_CHECK_CONDITION, etc.)
    uint8_t status = scsi::STATUS_GOOD;

    // SCSI message byte (typically MSG_COMMAND_COMPLETE)
    uint8_t message = scsi::MSG_COMMAND_COMPLETE;

    // Data to return to the initiator (for READ-type commands, INQUIRY, etc.)
    // Empty for write commands and no-data commands.
    std::vector<uint8_t> data_in;

    // Number of bytes the target expects to receive from the initiator
    // (for WRITE-type commands, MODE SELECT, etc.)
    // Zero for read commands and no-data commands.
    // The bus collects this many bytes in DATA_OUT phase before calling execute().
    size_t data_out_expected = 0;
};

// Interface for devices on the SCSI bus (hard discs, LaserDisc players, etc.)
//
// Targets know nothing about the bus protocol, register interface, or timing.
// They receive complete CDBs and return results. The ScsiBus handles all
// protocol-level concerns (phase transitions, data buffering, status delivery).
struct ScsiTarget {
    virtual ~ScsiTarget() = default;

    // Process a complete SCSI command.
    //
    // For read-type commands: cdb contains the command, data_out is empty.
    //   Return data_in populated with the response data.
    //
    // For write-type commands: cdb contains the command, data_out contains
    //   the data written by the initiator. Return data_in empty.
    //
    // For no-data commands: both data_out and data_in are empty.
    //
    // The first call with a new CDB may return data_out_expected > 0 to
    // indicate the bus should collect data before calling execute() again.
    virtual ScsiCommandResult execute(std::span<const uint8_t> cdb,
                                      std::span<const uint8_t> data_out) = 0;

    // Whether this target is present and responding on the bus.
    // A target that returns false will cause SELECTION to fail.
    virtual bool is_present() const = 0;

    // Human-readable device type for gRPC enumeration (e.g. "hard-disc", "test-device").
    virtual std::string_view device_type() const = 0;

    // Human-readable description for gRPC enumeration.
    virtual std::string_view description() const = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_TARGET_HPP
