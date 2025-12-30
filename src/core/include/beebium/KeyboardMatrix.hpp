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

#include <atomic>
#include <cstdint>

namespace beebium {

// BBC Micro keyboard matrix (10 columns x 10 rows)
//
// The BBC keyboard is a matrix of 10 columns and 10 rows. Each key
// position corresponds to a (row, column) pair. Links (DIP switches)
// on the bottom of the keyboard also appear in this matrix.
//
// Key positions:
// - Columns 0-9: Physical keyboard columns
// - Rows 0-9: Physical keyboard rows
// - Row 0 is special: contains startup links (mode selection)
//
// Thread Safety:
// This class is thread-safe. It can be written from one thread (e.g., gRPC)
// and read from another (e.g., emulator main loop) without external locking.
// All operations use atomic memory accesses to ensure visibility and prevent
// torn reads/writes.
//
class KeyboardMatrix {
public:
    static constexpr uint8_t NUM_COLUMNS = 10;
    static constexpr uint8_t NUM_ROWS = 10;

    // Set a key as pressed (thread-safe)
    void key_down(uint8_t row, uint8_t column) {
        if (row < NUM_ROWS && column < NUM_COLUMNS) {
            // Atomic fetch_or sets the bit without racing with other operations
            columns_[column].fetch_or(static_cast<uint16_t>(1 << row),
                                      std::memory_order_release);
        }
    }

    // Set a key as released (thread-safe)
    void key_up(uint8_t row, uint8_t column) {
        if (row < NUM_ROWS && column < NUM_COLUMNS) {
            // Atomic fetch_and clears the bit without racing
            columns_[column].fetch_and(static_cast<uint16_t>(~(1 << row)),
                                       std::memory_order_release);
        }
    }

    // Check if a specific key is pressed (thread-safe)
    bool is_key_pressed(uint8_t row, uint8_t column) const {
        if (row < NUM_ROWS && column < NUM_COLUMNS) {
            return (columns_[column].load(std::memory_order_acquire) & (1 << row)) != 0;
        }
        return false;
    }

    // Read a column's state (bitmask of pressed rows) (thread-safe)
    uint16_t read_column(uint8_t column) const {
        if (column < NUM_COLUMNS) {
            return columns_[column].load(std::memory_order_acquire);
        }
        return 0;
    }

    // Get all keys pressed in a row (bit per column) (thread-safe snapshot)
    uint16_t get_row_state(uint8_t row) const {
        if (row >= NUM_ROWS) return 0;
        uint16_t state = 0;
        for (uint8_t col = 0; col < NUM_COLUMNS; ++col) {
            if (columns_[col].load(std::memory_order_acquire) & (1 << row)) {
                state |= (1 << col);
            }
        }
        return state;
    }

    // Check if any key in a column is pressed (optionally excluding row 0) (thread-safe)
    bool any_key_in_column(uint8_t column, bool exclude_row_0 = false) const {
        if (column >= NUM_COLUMNS) return false;
        uint16_t mask = exclude_row_0 ? 0x3FE : 0x3FF;  // Rows 1-9 or 0-9
        return (columns_[column].load(std::memory_order_acquire) & mask) != 0;
    }

    // Clear all pressed keys (thread-safe)
    void clear() {
        for (auto& col : columns_) {
            col.store(0, std::memory_order_release);
        }
    }

    // Set startup links for boot screen mode (0-7)
    //
    // The BBC Micro uses three startup links (6, 7, 8) in row 0 of the
    // keyboard matrix to determine the boot screen mode:
    //   Link 6 (row 0, col 7): Mode bit 2 (MSB)
    //   Link 7 (row 0, col 8): Mode bit 1
    //   Link 8 (row 0, col 9): Mode bit 0 (LSB)
    //
    // The MOS XORs the link readings with 7, so:
    //   All links BROKEN (0,0,0) -> Mode 7 (default)
    //   All links MADE (1,1,1) -> Mode 0
    //
    // Must be called before reset() to take effect during boot.
    void set_startup_screen_mode(uint8_t screen_mode) {
        if (screen_mode > 7) screen_mode = 7;

        // Calculate link bits: screen_mode = 7 XOR link_bits
        // So link_bits = 7 XOR screen_mode
        uint8_t link_bits = 7 ^ screen_mode;

        // Link 6 (bit 2) at column 7
        if (link_bits & 0x04) key_down(0, 7); else key_up(0, 7);
        // Link 7 (bit 1) at column 8
        if (link_bits & 0x02) key_down(0, 8); else key_up(0, 8);
        // Link 8 (bit 0) at column 9
        if (link_bits & 0x01) key_down(0, 9); else key_up(0, 9);
    }

    // Get current startup screen mode from link state (thread-safe)
    uint8_t startup_screen_mode() const {
        uint8_t link_bits = 0;
        if (is_key_pressed(0, 7)) link_bits |= 0x04;  // Link 6
        if (is_key_pressed(0, 8)) link_bits |= 0x02;  // Link 7
        if (is_key_pressed(0, 9)) link_bits |= 0x01;  // Link 8
        return 7 ^ link_bits;
    }

private:
    // Each element is a bitmask of rows pressed in that column
    // Bit N set means row N is pressed in this column
    // Using std::atomic for thread-safe access from gRPC and emulator threads
    std::atomic<uint16_t> columns_[NUM_COLUMNS] = {};
};

} // namespace beebium
