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

// test_keyboard_helpers.hpp
//
// Shared keyboard input helper functions for tests that need to type
// strings into the emulated BBC Micro.

#ifndef BEEBIUM_TEST_KEYBOARD_HELPERS_HPP
#define BEEBIUM_TEST_KEYBOARD_HELPERS_HPP

#include <beebium/FrameRenderer.hpp>
#include <sstream>
#include <string>

namespace beebium::test {

// BBC keyboard matrix: ASCII to (row, column) mapping
// Returns (row, column) packed as (row << 4) | column, or 0xFF if not mappable
inline uint8_t ascii_to_keycode(uint8_t ascii) {
    switch (ascii) {
        // Essential control characters
        case '\r': return 0x49;  // RETURN (row 4, col 9)

        // Space
        case ' ':  return 0x62;  // SPACE (row 6, col 2)

        // Digits 0-9
        case '0':  return 0x27;  // row 2, col 7
        case '1':  return 0x30;  // row 3, col 0
        case '2':  return 0x31;  // row 3, col 1
        case '3':  return 0x11;  // row 1, col 1
        case '4':  return 0x12;  // row 1, col 2
        case '5':  return 0x13;  // row 1, col 3
        case '6':  return 0x34;  // row 3, col 4
        case '7':  return 0x24;  // row 2, col 4
        case '8':  return 0x15;  // row 1, col 5
        case '9':  return 0x26;  // row 2, col 6

        // Letters A-Z (BBC BASIC uses uppercase for keywords)
        case 'A': case 'a': return 0x41;  // row 4, col 1
        case 'B': case 'b': return 0x64;  // row 6, col 4
        case 'C': case 'c': return 0x52;  // row 5, col 2
        case 'D': case 'd': return 0x32;  // row 3, col 2
        case 'E': case 'e': return 0x22;  // row 2, col 2
        case 'F': case 'f': return 0x43;  // row 4, col 3
        case 'G': case 'g': return 0x53;  // row 5, col 3
        case 'H': case 'h': return 0x54;  // row 5, col 4
        case 'I': case 'i': return 0x25;  // row 2, col 5
        case 'J': case 'j': return 0x45;  // row 4, col 5
        case 'K': case 'k': return 0x46;  // row 4, col 6
        case 'L': case 'l': return 0x56;  // row 5, col 6
        case 'M': case 'm': return 0x65;  // row 6, col 5
        case 'N': case 'n': return 0x55;  // row 5, col 5
        case 'O': case 'o': return 0x36;  // row 3, col 6
        case 'P': case 'p': return 0x37;  // row 3, col 7
        case 'Q': case 'q': return 0x10;  // row 1, col 0
        case 'R': case 'r': return 0x33;  // row 3, col 3
        case 'S': case 's': return 0x51;  // row 5, col 1
        case 'T': case 't': return 0x23;  // row 2, col 3
        case 'U': case 'u': return 0x35;  // row 3, col 5
        case 'V': case 'v': return 0x63;  // row 6, col 3
        case 'W': case 'w': return 0x21;  // row 2, col 1
        case 'X': case 'x': return 0x42;  // row 4, col 2
        case 'Y': case 'y': return 0x44;  // row 4, col 4
        case 'Z': case 'z': return 0x61;  // row 6, col 1

        // Essential punctuation for BASIC
        case ';':  return 0x57;  // row 5, col 7 (SHIFT gives +)
        case ':':  return 0x48;  // row 4, col 8 (SHIFT gives *)
        case ',':  return 0x66;  // row 6, col 6
        case '.':  return 0x67;  // row 6, col 7
        case '(':  return 0x15;  // row 1, col 5 (SHIFT+8)
        case ')':  return 0x26;  // row 2, col 6 (SHIFT+9)
        case '+':  return 0x57;  // row 5, col 7 (SHIFT+;)
        case '-':  return 0x17;  // row 1, col 7 (minus key)
        case '*':  return 0x48;  // row 4, col 8 (SHIFT+:)
        case '/':  return 0x68;  // row 6, col 8
        case '=':  return 0x17;  // row 1, col 7 (SHIFT+-)
        case '$':  return 0x12;  // row 1, col 2 (SHIFT+4)
        case '"':  return 0x31;  // row 3, col 1 (SHIFT+2)
        case '&':  return 0x34;  // row 3, col 4 (SHIFT+6)
        case '?':  return 0x68;  // row 6, col 8 (SHIFT+/)

        default: return 0xFF;  // Not mapped
    }
}

// Inject a key press into the keyboard
template<typename MachineType>
void press_key(MachineType& machine, uint8_t ascii_code) {
    uint8_t keycode = ascii_to_keycode(ascii_code);
    if (keycode != 0xFF) {
        uint8_t row = (keycode >> 4) & 0x0F;
        uint8_t col = keycode & 0x0F;
        machine.state().memory.system_via_peripheral.key_down(row, col);
    }
}

template<typename MachineType>
void release_key(MachineType& machine, uint8_t ascii_code) {
    uint8_t keycode = ascii_to_keycode(ascii_code);
    if (keycode != 0xFF) {
        uint8_t row = (keycode >> 4) & 0x0F;
        uint8_t col = keycode & 0x0F;
        machine.state().memory.system_via_peripheral.key_up(row, col);
    }
}

// SHIFT key position on BBC keyboard
constexpr uint8_t SHIFT_ROW = 0;
constexpr uint8_t SHIFT_COL = 0;

// Returns true if character requires SHIFT key
inline bool needs_shift(uint8_t ascii) {
    switch (ascii) {
        case '+':
        case '*':
        case '=':
        case '$':
        case '"':
        case '!':
        case '#':
        case '%':
        case '&':
        case '\'':  // apostrophe (SHIFT+7)
        case '(':
        case ')':
        case '<':
        case '>':
        case '?':
        case '@':
        case '^':
        case '_':
        case '{':
        case '}':
        case '|':
        case '~':
            return true;
        default:
            return false;
    }
}

// Type a string with SHIFT key support for shifted characters
// Default cycles_per_key of 100000 is the minimum reliable speed.
// Speeds below this may lose characters.
template<typename MachineType>
void type_string_with_shift(MachineType& machine, FrameRenderer& renderer,
                             const char* str, int cycles_per_key = 100000) {
    for (const char* p = str; *p; ++p) {
        uint8_t ch = static_cast<uint8_t>(*p);
        bool shift_needed = needs_shift(ch);

        // Press SHIFT if needed
        if (shift_needed) {
            machine.state().memory.system_via_peripheral.key_down(SHIFT_ROW, SHIFT_COL);
        }

        // Press the key
        press_key(machine, ch);

        // Run cycles while key is held
        for (int i = 0; i < cycles_per_key / 2; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Release the key
        release_key(machine, ch);

        // Release SHIFT if it was pressed
        if (shift_needed) {
            machine.state().memory.system_via_peripheral.key_up(SHIFT_ROW, SHIFT_COL);
        }

        // Run cycles after release
        for (int i = 0; i < cycles_per_key / 2; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }
}

// Read a string from MODE 7 screen memory starting at given row/col
template<typename MachineType>
std::string read_mode7_screen(MachineType& machine, int start_row, int start_col, int length) {
    std::string result;
    uint16_t addr = 0x7C00 + start_row * 40 + start_col;
    for (int i = 0; i < length; ++i) {
        uint8_t ch = machine.read(addr + i);
        // Convert teletext codes to printable ASCII where possible
        if (ch >= 0x20 && ch <= 0x7E) {
            result += static_cast<char>(ch);
        } else {
            result += '.';  // Non-printable
        }
    }
    return result;
}

// Type a multi-line BASIC program stored as a string.
// Each line is typed followed by RETURN.
template<typename MachineType>
void type_basic_program(MachineType& machine, FrameRenderer& renderer,
                        const std::string& program, int cycles_per_key = 100000) {
    std::istringstream stream(program);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            type_string_with_shift(machine, renderer, (line + "\r").c_str(), cycles_per_key);
        }
    }
}

} // namespace beebium::test

#endif // BEEBIUM_TEST_KEYBOARD_HELPERS_HPP
