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

#include <beebium/SidewaysRomHeader.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using beebium::parse_sideways_rom_header;

namespace {

// Build a minimal well-formed sideways ROM: header, title, optional version,
// then the "(C)" copyright block. Returns the image.
std::vector<uint8_t> build_rom(uint8_t type_byte, uint8_t binary_version,
                               const std::string& title, const std::string& version,
                               const std::string& copyright) {
    std::vector<uint8_t> rom(64, 0x00);
    rom[6] = type_byte;
    rom[8] = binary_version;

    size_t i = 9;
    for (char c : title) rom[i++] = static_cast<uint8_t>(c);
    rom[i++] = 0;  // title terminator
    if (!version.empty()) {
        for (char c : version) rom[i++] = static_cast<uint8_t>(c);
        rom[i++] = 0;  // version terminator
    }
    rom[7] = static_cast<uint8_t>(i);  // copyright offset = the leading 0 byte
    rom[i++] = 0;
    for (char c : copyright) rom[i++] = static_cast<uint8_t>(c);
    rom[i++] = 0;
    return rom;
}

}  // namespace

TEST_CASE("parse_sideways_rom_header: parses a well-formed language+service header",
          "[rom][header]") {
    auto rom = build_rom(0xC2, 5, "TEST", "1.5", "(C)2026 Beebium");
    auto h = parse_sideways_rom_header(rom);

    REQUIRE(h.recognised);
    REQUIRE(h.has_service_entry);
    REQUIRE(h.has_language_entry);
    REQUIRE_FALSE(h.has_relocation_address);
    REQUIRE_FALSE(h.supports_electron_firmkeys);
    REQUIRE(h.cpu_type == 2);
    REQUIRE(h.binary_version == 5);
    REQUIRE(h.title == "TEST");
    REQUIRE(h.version == "1.5");
    REQUIRE(h.copyright == "(C)2026 Beebium");
    REQUIRE(h.is_language());
    REQUIRE_FALSE(h.is_service_only());  // has both entries
}

TEST_CASE("parse_sideways_rom_header: a service-only ROM is classified as such",
          "[rom][header]") {
    auto rom = build_rom(0x82, 1, "UTILS", "", "(C)2026 Beebium");
    auto h = parse_sideways_rom_header(rom);

    REQUIRE(h.recognised);
    REQUIRE(h.has_service_entry);
    REQUIRE_FALSE(h.has_language_entry);
    REQUIRE(h.version.empty());
    REQUIRE(h.is_service_only());
    REQUIRE_FALSE(h.is_language());
}

TEST_CASE("parse_sideways_rom_header: relocation and firmkey flags", "[rom][header]") {
    auto rom = build_rom(0x30, 0, "X", "", "(C)2026 Me");  // bits 5 and 4 set
    auto h = parse_sideways_rom_header(rom);
    REQUIRE(h.recognised);
    REQUIRE(h.has_relocation_address);
    REQUIRE(h.supports_electron_firmkeys);
}

TEST_CASE("parse_sideways_rom_header: too small is not recognised", "[rom][header]") {
    std::vector<uint8_t> rom(4, 0x00);
    REQUIRE_FALSE(parse_sideways_rom_header(rom).recognised);
}

TEST_CASE("parse_sideways_rom_header: a missing (C) marker is not recognised",
          "[rom][header]") {
    std::vector<uint8_t> rom(64, 0x00);
    rom[7] = 20;  // points into zeroed bytes, no "(C)"
    REQUIRE_FALSE(parse_sideways_rom_header(rom).recognised);
}

#ifdef BEEBIUM_ROM_DIR
namespace {
std::vector<uint8_t> load_rom(const std::string& name) {
    std::ifstream file(std::string(BEEBIUM_ROM_DIR) + "/" + name, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("parse_sideways_rom_header: BBC BASIC is a language ROM", "[rom][header][roms]") {
    auto h = parse_sideways_rom_header(load_rom("bbc-basic_2.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.has_language_entry);
    REQUIRE_FALSE(h.has_service_entry);  // 6502 BASIC has no service entry
    REQUIRE(h.title == "BASIC");
    REQUIRE(h.copyright == "(C)1982 Acorn");
    REQUIRE(h.is_language());
    REQUIRE_FALSE(h.contains_romfs);
}

TEST_CASE("parse_sideways_rom_header: Acorn DFS is a service ROM", "[rom][header][roms]") {
    auto h = parse_sideways_rom_header(load_rom("acorn-dfs_2_26.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.has_service_entry);
    REQUIRE_FALSE(h.has_language_entry);
    REQUIRE(h.title == "DFS");
    REQUIRE(h.version == "2.26");
    REQUIRE(h.is_service_only());
    REQUIRE_FALSE(h.contains_romfs);  // DFS is code, not a CFS block chain
}

TEST_CASE("parse_sideways_rom_header: Acorn ADFS title and version", "[rom][header][roms]") {
    auto h = parse_sideways_rom_header(load_rom("acorn-adfs_1_30.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.title == "Acorn ADFS");
    REQUIRE(h.version == "1.30");
    REQUIRE_FALSE(h.contains_romfs);
}

TEST_CASE("parse_sideways_rom_header: an OS ROM is not a recognised sideways ROM",
          "[rom][header][roms]") {
    auto h = parse_sideways_rom_header(load_rom("acorn-mos_1_20.rom"));
    REQUIRE_FALSE(h.recognised);
}
#endif

#ifdef BEEBIUM_TEST_ASSETS_DIR
namespace {
std::vector<uint8_t> load_romfs_image(const std::string& name) {
    std::ifstream file(std::string(BEEBIUM_TEST_ASSETS_DIR) + "/roms/romfs/" + name,
                       std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("parse_sideways_rom_header: Electron Hopper is language + service + ROMFS",
          "[rom][header][romfs]") {
    auto h = parse_sideways_rom_header(load_romfs_image("Electron_Hopper.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.has_service_entry);
    REQUIRE(h.has_language_entry);  // type &C2: an Acornsoft auto-start cartridge
    REQUIRE(h.contains_romfs);
    REQUIRE(h.romfs_data_offset == 0xBB);  // documented title-block offset (&80BB)
}

TEST_CASE("parse_sideways_rom_header: SNAPPER (oaknut-authored) is service + ROMFS",
          "[rom][header][romfs]") {
    auto h = parse_sideways_rom_header(load_romfs_image("SNAPPER.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.is_service_only());
    REQUIRE(h.contains_romfs);
}

TEST_CASE("parse_sideways_rom_header: Zalaga is ROMFS with no title block",
          "[rom][header][romfs]") {
    auto h = parse_sideways_rom_header(load_romfs_image("Zalaga.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.is_service_only());
    REQUIRE(h.contains_romfs);
}

TEST_CASE("parse_sideways_rom_header: BBC Master Demonstration is service + ROMFS",
          "[rom][header][romfs]") {
    auto h = parse_sideways_rom_header(
        load_romfs_image("BBC_Master_Demonstration_Cartridge_1.rom"));
    REQUIRE(h.recognised);
    REQUIRE(h.is_service_only());
    REQUIRE(h.contains_romfs);
}
#endif
