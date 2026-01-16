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

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace beebium {

// Slot type enum for runtime configuration
enum class SlotType : uint8_t {
    Empty,  // Returns 0xFF, ignores writes
    Rom,    // Returns data, ignores writes
    Ram     // Returns data, accepts writes
};

// Runtime-configurable 16KB memory slot.
// Can be configured as Empty, Rom, or Ram at runtime.
// This is used for sideways ROM/RAM sockets that can be dynamically configured.
//
// Behavior by type:
//   Empty: read() returns 0xFF, write() is ignored
//   Rom:   read() returns data, write() is ignored
//   Ram:   read() returns data, write() updates data
//
class ConfigurableSlot {
    std::array<uint8_t, 16384> data_{};
    SlotType type_ = SlotType::Empty;
    std::string image_name_;  // Source filename/identifier if loaded

public:
    static constexpr size_t size = 16384;

    ConfigurableSlot() {
        // Empty slots return 0xFF, initialise data to match
        data_.fill(0xFF);
    }

    explicit ConfigurableSlot(SlotType type) : type_(type) {
        // Empty slots return 0xFF, ROM slots typically filled with 0xFF until loaded
        data_.fill(0xFF);
    }

    // Read from slot - behavior depends on type
    uint8_t read(uint16_t offset) const {
        switch (type_) {
            case SlotType::Empty:
                return 0xFF;
            case SlotType::Rom:
            case SlotType::Ram:
                return data_[offset % size];
        }
        return 0xFF;
    }

    // Write to slot - only affects Ram type
    void write(uint16_t offset, uint8_t value) {
        if (type_ == SlotType::Ram) {
            data_[offset % size] = value;
        }
        // Empty and Rom types ignore writes
    }

    // Get/set slot type
    SlotType type() const { return type_; }

    void set_type(SlotType type) {
        type_ = type;
        // When changing to Empty, optionally clear data
        // For now, preserve data to allow inspecting previous contents
    }

    // Check if slot has been populated (non-0xFF content for Rom, any data for Ram)
    bool is_populated() const {
        if (type_ == SlotType::Empty) {
            return false;
        }
        // Check if any byte differs from 0xFF (empty ROM)
        for (const auto& byte : data_) {
            if (byte != 0xFF) return true;
        }
        return false;
    }

    // Check slot properties
    bool is_empty() const { return type_ == SlotType::Empty; }
    bool is_rom() const { return type_ == SlotType::Rom; }
    bool is_ram() const { return type_ == SlotType::Ram; }
    bool is_writable() const { return type_ == SlotType::Ram; }

    // Load data into slot (typically ROM image or pre-loaded RAM)
    void load(std::span<const uint8_t> src) {
        size_t copy_len = std::min(src.size(), size);
        std::memcpy(data_.data(), src.data(), copy_len);
        // Fill remainder with 0xFF if source is smaller
        if (copy_len < size) {
            std::memset(data_.data() + copy_len, 0xFF, size - copy_len);
        }
    }

    void load(const uint8_t* src, size_t len) {
        load(std::span<const uint8_t>(src, len));
    }

    // Clear slot contents (0x00 for RAM, 0xFF for Empty/ROM)
    void clear() {
        if (type_ == SlotType::Ram) {
            data_.fill(0x00);
        } else {
            data_.fill(0xFF);
        }
    }

    // Direct access for initialization/testing
    uint8_t* data() noexcept { return data_.data(); }
    const uint8_t* data() const noexcept { return data_.data(); }

    // Image name tracking (for UI/debugging)
    const std::string& image_name() const { return image_name_; }
    void set_image_name(std::string_view name) { image_name_ = std::string(name); }
    void clear_image_name() { image_name_.clear(); }
};

} // namespace beebium
