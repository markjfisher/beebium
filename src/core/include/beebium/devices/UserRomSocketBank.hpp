// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace beebium {

// The five user-replaceable ROM sockets shared by every B+ variant.
// Each socket holds a 32 KiB device, presented to the BBC as two 16 KiB
// halves at adjacent slot numbers:
//
//   IC35 -> slots 2 (lo) / 3 (hi)
//   IC44 -> slots 4 (lo) / 5 (hi)
//   IC57 -> slots 6 (lo) / 7 (hi)
//   IC62 -> slots 8 (lo) / 9 (hi)
//   IC68 -> slots 10 (lo) / 11 (hi)
//
// Each half is an independent ConfigurableSlot so the user can install
// ROMs, leave sockets empty, or (with a third-party sideways RAM
// module, which was commonplace) configure the slot as RAM.
//
// This component is composed by both ModelBPlusHardware and
// ModelBPlus128KHardware: the user-replaceable sockets behave
// identically on both variants. The two classes differ only in what
// they wire to the *non*-user slots (12/13/14/15 and 0/1).
class UserRomSocketBank {
public:
    // 10 halves, indexed by position (0..9). Position to slot:
    //   position 0 -> slot 2 (IC35 lo)
    //   position 1 -> slot 3 (IC35 hi)
    //   ...
    //   position 9 -> slot 11 (IC68 hi)
    static constexpr size_t num_slots = 10;
    static constexpr uint8_t first_slot = 2;
    static constexpr uint8_t last_slot = 11;

    static constexpr bool owns(uint8_t slot) {
        return slot >= first_slot && slot <= last_slot;
    }

    UserRomSocketBank() {
        // Default kind is Empty: an unpopulated socket on real hardware
        // returns open bus (0xFF) and ignores writes.
        for (auto& s : slots_) s.set_type(SlotType::Empty);
    }

    // Per-slot access. at_slot(2) returns the IC35 lo socket, etc.
    // Returns nullptr (via the pointer overload) when the slot isn't
    // in [2..11]. Used by Hardware classes to bind each slot into
    // their BankedMemory template pack and to forward mutator calls.
    ConfigurableSlot& at_slot(uint8_t slot) {
        return slots_[slot - first_slot];
    }
    const ConfigurableSlot& at_slot(uint8_t slot) const {
        return slots_[slot - first_slot];
    }

    ConfigurableSlot* slot_for(uint8_t slot) {
        return owns(slot) ? &slots_[slot - first_slot] : nullptr;
    }
    const ConfigurableSlot* slot_for(uint8_t slot) const {
        return owns(slot) ? &slots_[slot - first_slot] : nullptr;
    }

    // Mutator helpers used by Hardware-level load_sideways_rom etc.
    // Each returns true if the slot was within range and was applied,
    // false otherwise (so the caller can decide what to do with non-user
    // slots like the BASIC pair or SRAM banks).
    bool load_rom(uint8_t slot, const uint8_t* data, size_t len,
                  std::string_view image_name) {
        auto* dst = slot_for(slot);
        if (!dst) return false;
        const size_t copy_len = std::min(len, ConfigurableSlot::size);
        dst->set_type(SlotType::Rom);
        dst->load(data, copy_len);
        if (!image_name.empty()) dst->set_image_name(image_name);
        return true;
    }

    bool load_data(uint8_t slot, const uint8_t* data, size_t len,
                   std::string_view image_name) {
        auto* dst = slot_for(slot);
        if (!dst) return false;
        const size_t copy_len = std::min(len, ConfigurableSlot::size);
        dst->load(data, copy_len);
        if (!image_name.empty()) dst->set_image_name(image_name);
        return true;
    }

    bool configure_as_ram(uint8_t slot) {
        auto* dst = slot_for(slot);
        if (!dst) return false;
        dst->set_type(SlotType::Ram);
        dst->clear_image_name();
        return true;
    }

    bool configure_as_empty(uint8_t slot) {
        auto* dst = slot_for(slot);
        if (!dst) return false;
        dst->set_type(SlotType::Empty);
        dst->clear_image_name();
        return true;
    }

    // Uniform per-slot query. Returns Empty for slots outside [2..11];
    // callers test owns(slot) to dispatch.
    SlotInfo slot_info(uint8_t slot) const {
        if (!owns(slot)) return {};
        const auto& s = slots_[slot - first_slot];
        return {s.type(), s.is_populated(), std::string(s.image_name())};
    }

private:
    std::array<ConfigurableSlot, num_slots> slots_{};
};

}  // namespace beebium
