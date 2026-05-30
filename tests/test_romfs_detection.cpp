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

#include <beebium/RomFsDetection.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using beebium::romfs::contains_romfs;
using beebium::romfs::crc16_xmodem;
using beebium::romfs::find_first_block;
using beebium::romfs::validate_cfs_header;

TEST_CASE("crc16_xmodem matches the check value", "[romfs][crc]") {
    const std::string check = "123456789";
    std::vector<std::uint8_t> data(check.begin(), check.end());
    REQUIRE(crc16_xmodem(data) == 0x31C3);
    REQUIRE(crc16_xmodem(std::vector<std::uint8_t>{}) == 0x0000);
}

namespace {
// Build a minimal CFS header block with a valid header CRC. All fixed fields
// are zero; only the sync byte, name, NUL and computed CRC differ.
std::vector<std::uint8_t> build_block(const std::string& name) {
    std::vector<std::uint8_t> block;
    block.push_back(0x2A);                               // sync
    for (char c : name) block.push_back(static_cast<std::uint8_t>(c));
    block.push_back(0x00);                               // NUL terminator
    for (int i = 0; i < 17; ++i) block.push_back(0x00);  // load/exec/blk/len/flag/end
    auto crc_input = std::span<const std::uint8_t>(block.data() + 1, block.size() - 1);
    std::uint16_t crc = crc16_xmodem(crc_input);
    block.push_back(static_cast<std::uint8_t>(crc >> 8));
    block.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    return block;
}
}  // namespace

TEST_CASE("validate_cfs_header accepts a well-formed block", "[romfs]") {
    auto block = build_block("TEST");
    std::size_t data_offset = 0;
    std::uint16_t data_length = 99;
    REQUIRE(validate_cfs_header(block, 0, data_offset, data_length));
    REQUIRE(data_length == 0);
    REQUIRE(data_offset == block.size());  // just past the header CRC
}

TEST_CASE("validate_cfs_header rejects a corrupted header CRC", "[romfs]") {
    auto block = build_block("TEST");
    block.back() ^= 0xFF;  // flip a CRC byte
    std::size_t off = 0;
    std::uint16_t len = 0;
    REQUIRE_FALSE(validate_cfs_header(block, 0, off, len));
}

TEST_CASE("validate_cfs_header rejects a name longer than 10 characters", "[romfs]") {
    auto block = build_block("ELEVENCHARS");  // 11 characters
    std::size_t off = 0;
    std::uint16_t len = 0;
    REQUIRE_FALSE(validate_cfs_header(block, 0, off, len));
}

TEST_CASE("validate_cfs_header rejects a missing sync byte", "[romfs]") {
    auto block = build_block("TEST");
    block[0] = 0x23;  // not '*'
    std::size_t off = 0;
    std::uint16_t len = 0;
    REQUIRE_FALSE(validate_cfs_header(block, 0, off, len));
}

TEST_CASE("contains_romfs is false on random / empty bytes", "[romfs]") {
    REQUIRE_FALSE(contains_romfs(std::vector<std::uint8_t>(16384, 0xFF)));
    REQUIRE_FALSE(contains_romfs(std::vector<std::uint8_t>(16384, 0x00)));
    REQUIRE_FALSE(contains_romfs(std::vector<std::uint8_t>{}));
}

#ifdef BEEBIUM_TEST_ASSETS_DIR
namespace {
std::vector<std::uint8_t> load_romfs_image(const std::string& name) {
    std::ifstream file(std::string(BEEBIUM_TEST_ASSETS_DIR) + "/roms/romfs/" + name,
                       std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("find_first_block locates Hopper's title block at the documented offset",
          "[romfs][corpus]") {
    auto rom = load_romfs_image("Electron_Hopper.rom");
    auto first = find_first_block(rom);
    REQUIRE(first.has_value());
    // The doc anchors Hopper's title block at &80BB (image offset 0xBB).
    REQUIRE(*first == 0xBB);
}

TEST_CASE("contains_romfs holds for every image in the ROMFS corpus",
          "[romfs][corpus]") {
    for (const char* name : {
             "Electron_Hopper.rom",
             "Electron_Snapper.rom",
             "Electron_Starship_Command_1.rom",
             "Electron_Starship_Command_2.rom",
             "Electron_Tree_Of_Knowledge_1.rom",
             "Electron_Tree_Of_Knowledge_2.rom",
             "Electron_Countdown_To_Doom_1.rom",
             "Electron_Countdown_To_Doom_2.rom",
             "BBC_Master_Demonstration_Cartridge_1.rom",
             "BBC_Master_Demonstration_Cartridge_2.rom",
             "Zalaga.rom",
             "SNAPPER.rom",
         }) {
        INFO(name);
        REQUIRE(contains_romfs(load_romfs_image(name)));
    }
}
#endif

#ifdef BEEBIUM_ROM_DIR
namespace {
std::vector<std::uint8_t> load_system_rom(const std::string& name) {
    std::ifstream file(std::string(BEEBIUM_ROM_DIR) + "/" + name, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("contains_romfs is false for BBC system ROMs", "[romfs][corpus]") {
    for (const char* name : {
             "bbc-basic_2.rom",
             "acorn-dfs_2_26.rom",
             "acorn-adfs_1_30.rom",
             "acorn-mos_1_20.rom",
         }) {
        INFO(name);
        REQUIRE_FALSE(contains_romfs(load_system_rom(name)));
    }
}
#endif
