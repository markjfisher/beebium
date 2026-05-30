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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Detect ROM Filing System (ROMFS) data inside a sideways ROM image. See
// docs/romfs-detection.md for the full algorithm and rationale. In summary: a
// ROMFS ROM is an ordinary service ROM that hands the MOS a stream of CFS
// blocks; finding one CRC-valid block header in the image is conclusive.

namespace beebium::romfs {

constexpr uint8_t kCfsSync     = 0x2A;  // '*'  header block
constexpr uint8_t kCfsContinue = 0x23;  // '#'  data-only continuation block
constexpr uint8_t kCfsEndOfFs  = 0x2B;  // '+'  end of the filing system

// Earliest offset at which a ROM body (and so a CFS block chain) can begin.
constexpr std::size_t kRomBodyStart = 9;

// CRC-16/XMODEM: polynomial 0x1021, init 0, no reflection. Stored big-endian
// on the ROM. Self-check: crc16_xmodem("123456789") == 0x31C3.
inline std::uint16_t crc16_xmodem(std::span<const std::uint8_t> data) {
    std::uint16_t crc = 0;
    for (std::uint8_t byte : data) {
        crc ^= static_cast<std::uint16_t>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000)
                      ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                      : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

// Try to validate a CFS header block at `pos`. On success, sets `data_offset`
// to the first data byte (just past the header CRC) and `data_length` to the
// block's data byte count, and returns true. Returns false on any structural
// or CRC mismatch.
inline bool validate_cfs_header(std::span<const std::uint8_t> rom,
                                std::size_t pos,
                                std::size_t& data_offset,
                                std::uint16_t& data_length) {
    if (pos >= rom.size() || rom[pos] != kCfsSync) {
        return false;
    }

    // Name: ASCII up to a NUL, at most 10 characters.
    std::size_t name_start = pos + 1;
    std::size_t nul = name_start;
    while (nul < rom.size() && rom[nul] != 0x00) {
        ++nul;
    }
    if (nul >= rom.size()) return false;          // unterminated
    if (nul - name_start > 10) return false;      // name too long

    std::size_t fixed_start = nul + 1;            // 17 fixed bytes follow the NUL
    std::size_t crc_start   = fixed_start + 17;
    std::size_t crc_end     = crc_start + 2;      // 2-byte big-endian header CRC
    if (crc_end > rom.size()) return false;       // runs off the end

    std::uint16_t stored =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(rom[crc_start]) << 8) |
        rom[crc_start + 1];
    std::uint16_t computed =
        crc16_xmodem(rom.subspan(name_start, crc_start - name_start));
    if (stored != computed) return false;

    // Fixed fields after the NUL: load(4) exec(4) block#(2) length(2) flag(1)
    // end(4). The block data length is the little-endian word at byte 10.
    data_length =
        static_cast<std::uint16_t>(rom[fixed_start + 10]) |
        (static_cast<std::uint16_t>(rom[fixed_start + 11]) << 8);
    data_offset = crc_end;
    return true;
}

// Scan for the first CRC-valid CFS header block in the image, anywhere from
// the body start onwards. Returns its offset (the sync byte), or nullopt if
// the image contains no ROMFS data.
inline std::optional<std::size_t> find_first_block(std::span<const std::uint8_t> rom) {
    for (std::size_t pos = kRomBodyStart; pos < rom.size(); ++pos) {
        if (rom[pos] != kCfsSync) continue;
        std::size_t data_offset = 0;
        std::uint16_t data_length = 0;
        if (validate_cfs_header(rom, pos, data_offset, data_length)) {
            return pos;
        }
    }
    return std::nullopt;
}

// Convenience: true if the image carries ROM Filing System data.
inline bool contains_romfs(std::span<const std::uint8_t> rom) {
    return find_first_block(rom).has_value();
}

}  // namespace beebium::romfs
