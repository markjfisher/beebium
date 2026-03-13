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

#include "TubeParasitePort.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace beebium {

// Memory map for the 6502 second processor (cheese wedge).
//
// 64 KB RAM with a 2 KB boot ROM overlaid at &F800-&FFFF during boot mode.
// Tube registers at &FEF8-&FEFF punch through both ROM and RAM.
//
// Boot mode:
//   - Reads from &F800-&FFFF return ROM data (except &FEF8-&FEFF which are Tube).
//   - Writes to &F800-&FFFF go to underlying RAM (except &FEF8-&FEFF which are Tube).
//   - Terminated on first access to any Tube register address. One-way latch:
//     can only be re-entered via reset().
//
// Normal mode:
//   - All addresses are RAM.
//   - Tube registers at &FEF8-&FEFF suppress RAM access (CAS suppressed in hardware).
//
// Reference: 6502 Second Processor Service Manual, Sections 5.1-5.3.

class ParasiteMemoryMap {
public:
    // Construct with a reference to the Tube port and a 2 KB ROM image.
    // The ROM span must be exactly 2048 bytes.
    ParasiteMemoryMap(TubeParasitePort& tube_port, std::span<const uint8_t, 2048> rom)
        : tube_port_(tube_port)
        , rom_enabled_(true)
    {
        ram_.fill(0);
        std::copy(rom.begin(), rom.end(), rom_.begin());
    }

    uint8_t read(uint16_t address) {
        if (is_tube_address(address)) {
            rom_enabled_ = false;
            return tube_port_.parasite_read(static_cast<uint8_t>(address & 7));
        }

        if (rom_enabled_ && address >= 0xF800) {
            return rom_[address & 0x7FF];
        }

        return ram_[address];
    }

    void write(uint16_t address, uint8_t value) {
        if (is_tube_address(address)) {
            rom_enabled_ = false;
            tube_port_.parasite_write(static_cast<uint8_t>(address & 7), value);
            return;
        }

        // In both boot and normal mode, writes always go to RAM.
        // In boot mode, the ROM shadows RAM for reads but writes pass through.
        ram_[address] = value;
    }

    bool boot_mode() const { return rom_enabled_; }

    // Re-enter boot mode. Called on hardware reset.
    // RAM contents are preserved (real DRAM retains data across reset).
    void reset() {
        rom_enabled_ = true;
    }

    // Direct RAM access for testing and DMA-like operations.
    uint8_t& ram(uint16_t address) { return ram_[address]; }
    const uint8_t& ram(uint16_t address) const { return ram_[address]; }

private:
    static bool is_tube_address(uint16_t address) {
        return address >= 0xFEF8 && address <= 0xFEFF;
    }

    TubeParasitePort& tube_port_;
    std::array<uint8_t, 65536> ram_;
    std::array<uint8_t, 2048> rom_;
    bool rom_enabled_;
};

}  // namespace beebium
