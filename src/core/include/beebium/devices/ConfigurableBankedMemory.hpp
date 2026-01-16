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

namespace beebium {

// ConfigurableBankedMemory models a ROM/RAM expansion board with 16 independent
// slots that can be individually configured at runtime.
//
// Unlike the stock BBC Model B (which has 4 physical sockets with aliasing),
// a ROM/RAM board provides full 4-bit decoding so each of the 16 ROMSEL values
// addresses a unique slot. This is typical of third-party expansion boards
// like those from Watford Electronics or modern replacements.
//
// Common configurations:
//
// jsbeeb-style layout:
//   Slots 0-7: RAM (sideways RAM banks)
//   Slot 8-12: Empty or user ROMs
//   Slot 13: ADFS ROM
//   Slot 14: DFS ROM
//   Slot 15: BASIC ROM
//
// Each slot can be configured as Empty, Rom, or Ram at runtime.
//
class ConfigurableBankedMemory {
    std::array<ConfigurableSlot, 16> slots_;
    uint8_t selected_bank_ = 0;

public:
    static constexpr size_t num_banks = 16;
    static constexpr size_t bank_size = 16384;

    ConfigurableBankedMemory() = default;

    // Initialize with a specific layout
    enum class Layout {
        AllEmpty,    // All slots empty (default)
        JsbeebStyle  // Slots 0-7 RAM, 8-15 ROM
    };

    explicit ConfigurableBankedMemory(Layout layout) {
        if (layout == Layout::JsbeebStyle) {
            init_jsbeeb_layout();
        }
    }

    // Initialize jsbeeb-style layout: slots 0-7 RAM, 8-15 ROM
    void init_jsbeeb_layout() {
        for (uint8_t i = 0; i < 8; ++i) {
            slots_[i].set_type(SlotType::Ram);
        }
        for (uint8_t i = 8; i < 16; ++i) {
            slots_[i].set_type(SlotType::Rom);
        }
    }

    // MemoryMappedDevice interface
    uint8_t read(uint16_t offset) const {
        return slots_[selected_bank_].read(offset);
    }

    void write(uint16_t offset, uint8_t value) {
        slots_[selected_bank_].write(offset, value);
    }

    // Bank selection (via ROMSEL)
    uint8_t selected_bank() const { return selected_bank_; }

    void select_bank(uint8_t bank) {
        selected_bank_ = bank & 0x0F;
    }

    // Direct slot access
    ConfigurableSlot& slot(uint8_t slot_idx) {
        return slots_[slot_idx % num_banks];
    }

    const ConfigurableSlot& slot(uint8_t slot_idx) const {
        return slots_[slot_idx % num_banks];
    }

    // Configure a slot's type
    void configure_slot(uint8_t slot_idx, SlotType type) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].set_type(type);
        }
    }

    // Load ROM data into a slot
    void load_rom(uint8_t slot_idx, const uint8_t* data, size_t len) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].load(data, len);
        }
    }

    // Load ROM and set type in one call
    void load_rom_to_slot(uint8_t slot_idx, const uint8_t* data, size_t len) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].set_type(SlotType::Rom);
            slots_[slot_idx].load(data, len);
        }
    }

    // Check if a bank is populated
    bool is_bank_populated(uint8_t bank) const {
        if (bank < num_banks) {
            return slots_[bank].is_populated();
        }
        return false;
    }

    // Get slot type
    SlotType bank_type(uint8_t bank) const {
        if (bank < num_banks) {
            return slots_[bank].type();
        }
        return SlotType::Empty;
    }

    // Direct bank access for debugger - side-effect free read
    uint8_t peek_bank(uint8_t bank, uint16_t offset) const {
        if (bank < num_banks) {
            return slots_[bank].read(offset);
        }
        return 0xFF;
    }

    // Direct bank access - normal read
    uint8_t read_bank(uint8_t bank, uint16_t offset) {
        if (bank < num_banks) {
            return slots_[bank].read(offset);
        }
        return 0xFF;
    }

    // Direct bank access - write
    void write_bank(uint8_t bank, uint16_t offset, uint8_t value) {
        if (bank < num_banks) {
            slots_[bank].write(offset, value);
        }
    }

    // Set image name for a slot
    void set_slot_image_name(uint8_t slot_idx, std::string_view name) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].set_image_name(name);
        }
    }

    // Get image name for a slot
    std::string_view slot_image_name(uint8_t slot_idx) const {
        if (slot_idx < num_banks) {
            return slots_[slot_idx].image_name();
        }
        return "";
    }

    // Clear a slot's contents
    void clear_slot(uint8_t slot_idx) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].clear();
            slots_[slot_idx].clear_image_name();
        }
    }

    // Trait to indicate this memory type does not have aliasing
    static constexpr bool has_aliasing = false;
};

} // namespace beebium
