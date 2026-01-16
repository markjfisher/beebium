// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "ConfigurableSlot.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace beebium {

// AliasedBankedMemory models the BBC Model B's 4-socket ROM arrangement with
// 4-way aliasing due to partial address decoding.
//
// The Model B has 4 physical ROM sockets, but the ROMSEL register provides a
// 4-bit (0-15) bank selection. With only partial address decoding on the PCB
// (2 address bits), each physical socket appears at 4 different slot numbers:
//
// | Socket | IC    | Aliased Slots | Typical Use |
// |--------|-------|---------------|-------------|
// |   0    | IC52  | 0, 4, 8, 12   | User ROM    |
// |   1    | IC88  | 1, 5, 9, 13   | DFS ROM     |
// |   2    | IC100 | 2, 6, 10, 14  | User ROM    |
// |   3    | IC101 | 3, 7, 11, 15  | BASIC ROM   |
//
// This means selecting ROMSEL=3 or ROMSEL=15 accesses the same physical socket
// (IC101, typically containing BASIC). This is why MOS finds BASIC at slot 15
// even though there are only 4 physical sockets.
//
// Each socket can be configured as Empty, Rom, or Ram at runtime, allowing
// simulation of different configurations (stock Model B, sideways RAM upgrades,
// etc.).
//
class AliasedBankedMemory {
    // 4 physical ROM sockets
    std::array<ConfigurableSlot, 4> sockets_;
    uint8_t selected_bank_ = 0;

public:
    static constexpr size_t num_sockets = 4;
    static constexpr size_t num_banks = 16;
    static constexpr size_t bank_size = 16384;

    // Socket identifiers matching BBC Model B PCB designations
    static constexpr uint8_t SOCKET_IC52 = 0;   // Slots 0, 4, 8, 12
    static constexpr uint8_t SOCKET_IC88 = 1;   // Slots 1, 5, 9, 13 (DFS)
    static constexpr uint8_t SOCKET_IC100 = 2;  // Slots 2, 6, 10, 14
    static constexpr uint8_t SOCKET_IC101 = 3;  // Slots 3, 7, 11, 15 (BASIC)

    // Socket names for display/debugging
    static constexpr std::string_view socket_names[4] = {
        "IC52", "IC88", "IC100", "IC101"
    };

    AliasedBankedMemory() = default;

    // Map a slot number (0-15) to a physical socket index (0-3)
    // Uses bottom 2 bits due to partial address decoding
    static constexpr uint8_t slot_to_socket(uint8_t slot) {
        return slot & 0x03;
    }

    // Get the aliased slots for a given socket
    static constexpr std::array<uint8_t, 4> socket_aliased_slots(uint8_t socket_idx) {
        return {
            static_cast<uint8_t>(socket_idx),
            static_cast<uint8_t>(socket_idx + 4),
            static_cast<uint8_t>(socket_idx + 8),
            static_cast<uint8_t>(socket_idx + 12)
        };
    }

    // MemoryMappedDevice interface
    uint8_t read(uint16_t offset) const {
        uint8_t socket_idx = slot_to_socket(selected_bank_);
        return sockets_[socket_idx].read(offset);
    }

    void write(uint16_t offset, uint8_t value) {
        uint8_t socket_idx = slot_to_socket(selected_bank_);
        sockets_[socket_idx].write(offset, value);
    }

    // Bank selection (via ROMSEL)
    uint8_t selected_bank() const { return selected_bank_; }

    void select_bank(uint8_t bank) {
        selected_bank_ = bank & 0x0F;
    }

    // Get the socket index for the currently selected bank
    uint8_t selected_socket() const {
        return slot_to_socket(selected_bank_);
    }

    // Direct socket access (0-3)
    ConfigurableSlot& socket(uint8_t socket_idx) {
        return sockets_[socket_idx % num_sockets];
    }

    const ConfigurableSlot& socket(uint8_t socket_idx) const {
        return sockets_[socket_idx % num_sockets];
    }

    // Configure a socket's type
    void configure_socket(uint8_t socket_idx, SlotType type) {
        if (socket_idx < num_sockets) {
            sockets_[socket_idx].set_type(type);
        }
    }

    // Load ROM data into a socket
    void load_rom_to_socket(uint8_t socket_idx, const uint8_t* data, size_t len) {
        if (socket_idx < num_sockets) {
            sockets_[socket_idx].load(data, len);
        }
    }

    // Configure a socket by slot number (maps to socket via aliasing)
    void configure_slot(uint8_t slot, SlotType type) {
        configure_socket(slot_to_socket(slot), type);
    }

    // Load ROM data by slot number (maps to socket via aliasing)
    void load_rom(uint8_t slot, const uint8_t* data, size_t len) {
        load_rom_to_socket(slot_to_socket(slot), data, len);
    }

    // Check if a bank appears populated (delegates to aliased socket)
    bool is_bank_populated(uint8_t bank) const {
        return sockets_[slot_to_socket(bank)].is_populated();
    }

    // Get socket type by bank number
    SlotType bank_type(uint8_t bank) const {
        return sockets_[slot_to_socket(bank)].type();
    }

    // Get socket type by socket index
    SlotType socket_type(uint8_t socket_idx) const {
        if (socket_idx < num_sockets) {
            return sockets_[socket_idx].type();
        }
        return SlotType::Empty;
    }

    // Direct bank access for debugger - side-effect free read
    // Note: multiple banks alias to the same socket
    uint8_t peek_bank(uint8_t bank, uint16_t offset) const {
        return sockets_[slot_to_socket(bank)].read(offset);
    }

    // Direct bank access - normal read
    uint8_t read_bank(uint8_t bank, uint16_t offset) {
        return sockets_[slot_to_socket(bank)].read(offset);
    }

    // Direct bank access - write
    void write_bank(uint8_t bank, uint16_t offset, uint8_t value) {
        sockets_[slot_to_socket(bank)].write(offset, value);
    }

    // Set image name for a socket
    void set_socket_image_name(uint8_t socket_idx, std::string_view name) {
        if (socket_idx < num_sockets) {
            sockets_[socket_idx].set_image_name(name);
        }
    }

    // Get image name for a socket
    std::string_view socket_image_name(uint8_t socket_idx) const {
        if (socket_idx < num_sockets) {
            return sockets_[socket_idx].image_name();
        }
        return "";
    }

    // Clear a socket's contents
    void clear_socket(uint8_t socket_idx) {
        if (socket_idx < num_sockets) {
            sockets_[socket_idx].clear();
            sockets_[socket_idx].clear_image_name();
        }
    }

    // Trait to indicate this memory type has aliasing
    static constexpr bool has_aliasing = true;
};

} // namespace beebium
