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

#ifndef BEEBIUM_BUS_STRETCHING_HPP
#define BEEBIUM_BUS_STRETCHING_HPP

#include <cstdint>

namespace beebium {

// BBC Micro 1MHz Bus Stretching
//
// The BBC Micro runs its CPU at 2MHz but many peripherals operate at 1MHz.
// When the CPU accesses a 1MHz peripheral, the bus controller synchronizes
// by inserting wait cycles (bus stretching) until the next 1MHz boundary.
//
// Address ranges requiring 1MHz bus stretching:
//   $FC00-$FCFF: FRED (external 1MHz I/O expansion)
//   $FD00-$FDFF: JIM (external 1MHz I/O expansion)
//   $FE00-$FE1F: SHEILA - CRTC, ACIA, Serial ULA (1MHz)
//   $FE40-$FE5F: SHEILA - System VIA (1MHz)
//   $FE60-$FE7F: SHEILA - User VIA (1MHz)
//   $FEC0-$FEDF: SHEILA - A/D converter (1MHz)
//
// Address ranges NOT requiring stretching (fast):
//   $FE20-$FE3F: Video ULA (clocked directly by video circuitry)
//   $FE80-$FE9F: Disc controller (WD1770)
//   $FEA0-$FEBF: Econet
//   $FEE0-$FEFF: Tube
//
// Reference: jsbeeb 6502.js is1MHzAccess() and polltimeAddr()
// Reference: beebjit bbc.c bbc_is_1MHz_address() and bbc_do_pre_read_write_tick_handling()

// Lookup table for SHEILA ($FE00-$FEFF) address ranges.
// Indexed by bits [7:5] of the address.
// 1 = 1MHz (requires stretching), 0 = fast (no stretching)
//
// Index 0 ($FE00-$FE1F): CRTC/ACIA/Serial ULA - 1MHz
// Index 1 ($FE20-$FE3F): Video ULA - fast
// Index 2 ($FE40-$FE5F): System VIA - 1MHz
// Index 3 ($FE60-$FE7F): User VIA - 1MHz
// Index 4 ($FE80-$FE9F): Disc controller - fast
// Index 5 ($FEA0-$FEBF): Econet - fast
// Index 6 ($FEC0-$FEDF): A/D converter - 1MHz
// Index 7 ($FEE0-$FEFF): Tube - fast
constexpr uint8_t kSheilaSlowdownMask = 0b01001101;

/// Returns true if the given address requires 1MHz bus stretching.
///
/// The BBC Micro's bus controller inserts wait cycles when the 2MHz CPU
/// accesses peripherals on the 1MHz bus. This function identifies which
/// addresses require this synchronization.
constexpr bool is_1mhz_access(uint16_t addr) {
    // Fast path: addresses below $FC00 never need stretching
    if (addr < 0xFC00) return false;

    // FRED ($FC00-$FCFF) and JIM ($FD00-$FDFF) are always 1MHz
    if (addr < 0xFE00) return true;

    // SHEILA ($FE00-$FEFF): consult lookup table based on bits [7:5]
    const uint8_t slot = (addr >> 5) & 7;
    return (kSheilaSlowdownMask >> slot) & 1;
}

/// Calculate the number of stretch cycles needed for a 1MHz bus access.
///
/// When the CPU accesses a 1MHz peripheral, the bus controller inserts
/// wait cycles to synchronize with the 1MHz clock. The number of extra
/// cycles depends on the current phase alignment.
///
/// The stretch adds 1-2 extra cycles on top of the normal instruction timing:
/// - From an even cycle (on a 1MHz edge): 1 extra cycle
/// - From an odd cycle (between edges): 2 extra cycles
///
/// This matches jsbeeb's formula which adds 1 + ((cycles ^ currentCycles) & 1)
/// extra cycles to the instruction time. For our purposes, cycle parity
/// determines alignment: odd cycles need an extra cycle to reach alignment.
///
/// @param cycle_count Current 2MHz cycle count
/// @return Number of extra cycles to insert (1 or 2)
constexpr uint8_t stretch_cycles(uint64_t cycle_count) {
    return 1 + static_cast<uint8_t>(cycle_count & 1);
}

// Open Bus Behavior Modes
//
// When reading from an unmapped address, the value returned depends on the
// bus speed and hardware characteristics:
//
// Accurate mode (default):
//   - Slow 1MHz regions: Pull-down resistors discharge bus to 0x00
//   - Fast 2MHz regions: Capacitance holds previous bus value
//   - FRED/JIM (0xFC00-0xFDFF): 74LS245 transceiver actively drives 0xFF
//
// JsbeebCompat mode:
//   - All unmapped addresses return 0xFF (matches jsbeeb Uint8Array default)
//   - Useful for differential testing against jsbeeb
//
// AllZero mode:
//   - All unmapped addresses return 0x00
//   - Alternative compatibility mode for some emulators
//
enum class OpenBusMode {
    Accurate,       // Bus tracking: slow→0x00, fast→last_bus_value, FRED/JIM→0xFF
    JsbeebCompat,   // Return 0xFF for all unmapped (matches jsbeeb Uint8Array default)
    AllZero         // Return 0x00 for all unmapped
};

/// Returns the appropriate value for reading from an unmapped address.
///
/// The returned value depends on the bus speed characteristics at the given address
/// and the configured open bus mode:
///
/// In Accurate mode:
/// - FRED/JIM (0xFC00-0xFDFF): 74LS245 transceiver actively drives 0xFF
/// - Other 1MHz regions: Pull-down resistors discharge bus to 0x00
/// - Fast 2MHz regions: Capacitance holds previous bus value
///
/// @param addr The unmapped address being read
/// @param last_bus_value The last value driven on the data bus (for fast cycles)
/// @param mode The open bus behavior mode
/// @return Value based on mode and bus speed characteristics
constexpr uint8_t unmapped_read_value(uint16_t addr, uint8_t last_bus_value, OpenBusMode mode) {
    switch (mode) {
        case OpenBusMode::Accurate:
            // FRED/JIM: 74LS245 transceiver actively drives 0xFF
            if (addr >= 0xFC00 && addr < 0xFE00) {
                return 0xFF;
            }
            // Slow 1MHz: pull-downs discharge to 0x00
            if (is_1mhz_access(addr)) {
                return 0x00;
            }
            // Fast 2MHz: capacitance holds previous bus value
            return last_bus_value;

        case OpenBusMode::JsbeebCompat:
            return 0xFF;  // jsbeeb's Uint8Array default

        case OpenBusMode::AllZero:
            return 0x00;
    }
    return 0xFF;  // Fallback for exhaustiveness
}

} // namespace beebium

#endif // BEEBIUM_BUS_STRETCHING_HPP
