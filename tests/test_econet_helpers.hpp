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

// test_econet_helpers.hpp
//
// Shared test infrastructure for Econet integration tests.
// Provides ROM loading, screen memory inspection, and ROM availability checks.

#ifndef BEEBIUM_TEST_ECONET_HELPERS_HPP
#define BEEBIUM_TEST_ECONET_HELPERS_HPP

#include <beebium/Machines.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace beebium::test {

inline std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

inline bool base_roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}

inline bool nfs_rom_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-nfs_3_34.rom");
}

// Search Mode 7 screen memory ($7C00-$7FFF) for a string.
inline bool screen_contains(ModelB& machine, const std::string& text) {
    for (uint16_t addr = 0x7C00; addr <= 0x7FFF - text.size(); ++addr) {
        bool match = true;
        for (size_t i = 0; i < text.size(); ++i) {
            if (machine.state().memory.read(addr + static_cast<uint16_t>(i)) !=
                static_cast<uint8_t>(text[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Dump first N rows of Mode 7 screen memory for debugging.
inline std::string dump_screen(ModelB& machine, int rows = 25) {
    std::string result;
    for (int row = 0; row < rows; ++row) {
        result += "Row " + std::to_string(row) + ": [";
        for (int col = 0; col < 40; ++col) {
            uint8_t ch = machine.state().memory.read(
                0x7C00 + static_cast<uint16_t>(row * 40 + col));
            if (ch >= 0x20 && ch < 0x7F) {
                result += static_cast<char>(ch);
            } else {
                result += '.';
            }
        }
        result += "]\n";
    }
    return result;
}

} // namespace beebium::test

#endif // BEEBIUM_TEST_ECONET_HELPERS_HPP
