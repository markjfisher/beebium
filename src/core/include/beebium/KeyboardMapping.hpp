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

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace beebium {

// BBC keyboard key mapping
// Maps characters and named keys to their internal key number and shift state.
//
// For character-producing keys, codepoint is the Unicode value (e.g., 'A' = 65).
// For non-character keys (function keys, cursor keys, etc.), codepoint is 0.
// The name field provides a human-readable identifier for all keys.
struct KeyMapping {
    char32_t codepoint;     // Unicode codepoint (0 for non-character keys)
    uint8_t ik_number;      // Internal key number: (row << 4) | column
    bool needs_shift;       // True if SHIFT key required
    const char* name;       // Key name (e.g., "A", "Return", "f0", "Up")

    // Extract row from ik_number
    uint8_t row() const { return (ik_number >> 4) & 0x0F; }

    // Extract column from ik_number
    uint8_t column() const { return ik_number & 0x0F; }

    // Check if this is a named-only key (no character output)
    bool is_named_only() const { return codepoint == 0; }
};

// Special key internal key numbers
constexpr uint8_t IK_SHIFT      = 0x00;  // Row 0, column 0
constexpr uint8_t IK_CTRL       = 0x01;  // Row 0, column 1
constexpr uint8_t IK_CAPS_LOCK  = 0x40;  // Row 4, column 0
constexpr uint8_t IK_SHIFT_LOCK = 0x50;  // Row 5, column 0
constexpr uint8_t IK_TAB        = 0x60;  // Row 6, column 0
constexpr uint8_t IK_ESCAPE     = 0x70;  // Row 7, column 0
constexpr uint8_t IK_SPACE      = 0x62;  // Row 6, column 2
constexpr uint8_t IK_RETURN     = 0x49;  // Row 4, column 9
constexpr uint8_t IK_DELETE     = 0x59;  // Row 5, column 9
constexpr uint8_t IK_COPY       = 0x69;  // Row 6, column 9
constexpr uint8_t IK_UP         = 0x39;  // Row 3, column 9
constexpr uint8_t IK_DOWN       = 0x29;  // Row 2, column 9
constexpr uint8_t IK_LEFT       = 0x19;  // Row 1, column 9
constexpr uint8_t IK_RIGHT      = 0x79;  // Row 7, column 9
constexpr uint8_t IK_F0         = 0x20;  // Row 2, column 0
constexpr uint8_t IK_F1         = 0x71;  // Row 7, column 1
constexpr uint8_t IK_F2         = 0x72;  // Row 7, column 2
constexpr uint8_t IK_F3         = 0x73;  // Row 7, column 3
constexpr uint8_t IK_F4         = 0x14;  // Row 1, column 4
constexpr uint8_t IK_F5         = 0x74;  // Row 7, column 4
constexpr uint8_t IK_F6         = 0x75;  // Row 7, column 5
constexpr uint8_t IK_F7         = 0x16;  // Row 1, column 6
constexpr uint8_t IK_F8         = 0x76;  // Row 7, column 6
constexpr uint8_t IK_F9         = 0x77;  // Row 7, column 7

// Map a Unicode codepoint to BBC key position
// Returns std::nullopt if character cannot be typed on BBC keyboard
std::optional<KeyMapping> char_to_key(char32_t codepoint);

// Map a key name to BBC key position
// Names are case-insensitive: "Return", "RETURN", "return" all work.
// Returns std::nullopt if name is not recognized.
std::optional<KeyMapping> key_by_name(std::string_view name);

// Map BBC key position back to character
// Returns the character for the given key position and shift state,
// or std::nullopt if not a character-producing key.
std::optional<char32_t> key_to_char(uint8_t ik_number, bool shifted);

// Get the name of a key by its internal key number
// Returns nullptr if ik_number is not in the mapping table.
const char* key_name(uint8_t ik_number);

// Check if a character can be typed on BBC keyboard
bool is_typeable(char32_t codepoint);

// Check if all characters in a UTF-8 string are typeable
// Returns true if all characters can be typed, false if any cannot.
bool is_typeable(std::string_view text);

// Get all character mappings
// Returns a span over the complete mapping table for iteration/export.
std::span<const KeyMapping> get_all_mappings();

// Get number of mappable characters
size_t mapping_count();

} // namespace beebium
