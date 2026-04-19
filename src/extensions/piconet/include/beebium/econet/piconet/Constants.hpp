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

#include <array>
#include <cstddef>
#include <cstdint>

// Constants extracted verbatim from the upstream Piconet firmware
// (https://github.com/jprayner/piconet) so the protocol layer cannot drift
// silently. Citations point at piconet/board/src/ paths in the upstream
// repo (mirrored locally at /Users/rjs/Code/piconet for development).
namespace beebium::piconet {

// Default station number set by the firmware on power-up.
// Source: piconet/board/src/econet.c line 97 (_listen_addresses[0] = 0x02).
inline constexpr std::uint8_t DEFAULT_STATION = 0x02;

// Hardcoded broadcast destination address.
// Source: piconet/board/src/econet.c line 97 (_listen_addresses[1] = 0xFF).
inline constexpr std::uint8_t BROADCAST_ADDR = 0xFF;

// MachinePeek immediate operation control byte. The firmware handles this
// inline and replies with a canned response without notifying the host.
// Source: piconet/board/src/econet.c line 603.
inline constexpr std::uint8_t MACHINE_PEEK_CTRL = 0x88;

// Canned MachinePeek response payload appended to the wire-level ACK.
// Bytes: machine_type=0x55 ('U' undefined), manufacturer=0x4A ('J' = JPR),
// version_minor=0x00, version_major=0x05 (32-bit client).
// Source: piconet/board/src/econet.c lines 605-616.
inline constexpr std::array<std::uint8_t, 4> MACHINE_PEEK_RESPONSE =
    {0x55, 0x4A, 0x00, 0x05};

// Firmware version we expect to interoperate with. The protocol library
// exposes these as constants but does NOT enforce -- PiconetBackend decides
// the policy on mismatch (warn vs. refuse to enable).
// Source: piconet/board/src/piconet.c lines 18-20.
inline constexpr std::uint32_t EXPECTED_FIRMWARE_MAJOR = 2;
inline constexpr std::uint32_t EXPECTED_FIRMWARE_MINOR = 0;

// Serial line settings. 8N1; line-oriented; commands end with CR, events
// end with LF.
// Source: README.md, piconet/driver/nodejs/src/driver/serial.ts line 18.
inline constexpr std::uint32_t BAUD_RATE = 115200;

// Line terminators are asymmetric. Commands sent to the device must end with
// '\r' (carriage return) -- the firmware command parser splits on '\r'
// (piconet/board/src/piconet.c line 430). Events emitted by the device end
// with '\n' (line feed) -- the printf format strings in
// piconet/board/src/piconet.c (e.g. lines 191, 200, 248) use "\n".
inline constexpr char COMMAND_TERMINATOR = '\r';
inline constexpr char EVENT_TERMINATOR = '\n';

// USB descriptors for the Raspberry Pi Pico that the Piconet firmware runs on.
// Source: TinyUSB device descriptor (RP2040 default VID/PID).
inline constexpr std::uint16_t USB_VENDOR_ID = 0x2E8A;
inline constexpr std::uint16_t USB_PRODUCT_ID = 0x000A;

// Maximum payload sizes the firmware can accept. Outbound TX data must fit.
// Source: piconet/board/src/piconet.c lines 23-30.
inline constexpr std::size_t MAX_TX_DATA_BYTES = 3500;
inline constexpr std::size_t MAX_TX_SCOUT_EXTRA_BYTES = 32;

// Firmware-internal timeout values that affect host-observable behaviour.
// PiconetBackend's watchdog should not fire before these elapse, otherwise
// it would race the firmware's own error reporting.
// Source: piconet/board/src/econet.c lines 10-15 and the wait_ack timeout
// at line 187 (200ms).
inline constexpr std::uint32_t SCOUT_ACK_TIMEOUT_MS = 200;
inline constexpr std::uint32_t DATA_ACK_TIMEOUT_MS = 200;
inline constexpr std::uint32_t LINE_JAMMED_TIMEOUT_MS = 10000;

}  // namespace beebium::piconet
