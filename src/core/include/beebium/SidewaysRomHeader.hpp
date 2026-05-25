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

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace beebium {

// Parsed details from a sideways ROM's standard header. Offset 0 is the ROM
// base (&8000 on the BBC). Layout (see docs/manuals/sidewrom.pdf):
//
//   &8000  JMP language (or 0 if not a language)
//   &8003  JMP service
//   &8006  ROM type byte (entry-point and CPU flags)
//   &8007  copyright offset: low byte of the address of the 0 byte before "(C)"
//   &8008  binary version number
//   &8009  title, zero-terminated
//   ...    optional version string, zero-terminated
//   ...    0 byte, "(C)..." copyright string, zero terminator
struct SidewaysRomHeader {
    // True if this looks like a legitimate sideways ROM: the copyright offset
    // points at a 0 byte immediately followed by "(C)", the marker the MOS
    // itself uses to recognise a ROM. An image that fails this is something
    // else (raw data, a non-standard image, an OS ROM, ...).
    bool recognised = false;

    bool has_service_entry = false;          // type byte bit 7
    bool has_language_entry = false;         // type byte bit 6
    bool has_relocation_address = false;     // type byte bit 5 (second processor)
    bool supports_electron_firmkeys = false; // type byte bit 4
    uint8_t cpu_type = 0;                    // type byte bits 0-3
    uint8_t binary_version = 0;              // byte at offset 8

    std::string title;      // offset 9, zero-terminated
    std::string version;    // optional, between the title and the copyright
    std::string copyright;  // "(C)..." string

    // A ROM with a language entry is a "language" (BASIC, View, ...). One with
    // only a service entry is a "service" ROM (DFS, ADFS, utilities, ROM filing
    // systems, ...). The header alone cannot distinguish, say, a ROM filing
    // system from any other service ROM; that needs deeper inspection.
    bool is_language() const { return recognised && has_language_entry; }
    bool is_service_only() const {
        return recognised && has_service_entry && !has_language_entry;
    }
};

namespace detail {

// Read a printable string from `start` up to (not including) `end` or the first
// 0 byte. BBC ROMs sometimes set bit 7 of title characters for emphasis, so we
// mask it; other non-printable bytes are dropped.
inline std::string read_rom_string(std::span<const uint8_t> rom, size_t start, size_t end) {
    std::string out;
    end = std::min(end, rom.size());
    for (size_t i = start; i < end; ++i) {
        if (rom[i] == 0) break;
        uint8_t c = rom[i] & 0x7F;
        if (c >= 0x20 && c < 0x7F) {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

}  // namespace detail

// Parse the sideways ROM header from a ROM image (offset 0 = ROM base).
//
// The copyright offset (byte 7) is the low byte of the copyright address; this
// is a faithful image offset for any real ROM, whose header strings sit well
// within the first 256 bytes. Always returns a struct; check `recognised`.
inline SidewaysRomHeader parse_sideways_rom_header(std::span<const uint8_t> rom) {
    SidewaysRomHeader header;
    if (rom.size() < 10) {
        return header;  // too small to hold even the fixed part of a header
    }

    const uint8_t type_byte = rom[6];
    header.has_service_entry = (type_byte & 0x80) != 0;
    header.has_language_entry = (type_byte & 0x40) != 0;
    header.has_relocation_address = (type_byte & 0x20) != 0;
    header.supports_electron_firmkeys = (type_byte & 0x10) != 0;
    header.cpu_type = type_byte & 0x0F;
    header.binary_version = rom[8];

    // Recognition: a 0 byte at the copyright offset, immediately followed by
    // the "(C)" the MOS looks for.
    const size_t copyright_offset = rom[7];
    if (copyright_offset + 3 >= rom.size() ||
        rom[copyright_offset] != 0x00 ||
        rom[copyright_offset + 1] != '(' ||
        rom[copyright_offset + 2] != 'C' ||
        rom[copyright_offset + 3] != ')') {
        return header;  // not a recognised sideways ROM
    }
    header.recognised = true;

    // Title at offset 9, terminated by a 0 byte before the copyright.
    size_t title_end = 9;
    while (title_end < copyright_offset && rom[title_end] != 0x00) {
        ++title_end;
    }
    header.title = detail::read_rom_string(rom, 9, title_end);

    // An optional version string sits between the title's terminator and the
    // copyright's leading 0 byte.
    size_t version_start = title_end + 1;
    if (version_start < copyright_offset) {
        header.version = detail::read_rom_string(rom, version_start, copyright_offset);
    }

    header.copyright = detail::read_rom_string(rom, copyright_offset + 1, rom.size());
    return header;
}

}  // namespace beebium
