// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "SerialSocket.hpp"

#include <concepts>
#include <string_view>

namespace beebium {

// Does this hardware fit the on-board serial socket (MC6850 ACIA + Serial ULA,
// with the RS423 port they drive)? This is "Axis A" of serial presence (see
// docs/discussion/serial-architecture-review.md): chips + RS423, a both-or-
// neither pair, detected by the presence of the serial_socket member. All
// current Model B variants provide it; a future Master Compact may omit it
// (the member is simply absent there), and the stack already guards on this
// concept (e.g. SerialService reports has_serial_socket = false when absent).
//
// Cassette is a SEPARATE axis (Axis B: present on Model A/B/B+/Master 128,
// absent on the Compact even when the chips are fitted). It must NOT be derived
// from this concept; it will be modelled alongside the cassette seam in a later
// follow-up, not here.
template<typename T>
concept HasSerialSocket = requires(T& hw) {
    { hw.serial_socket } -> std::same_as<SerialSocket&>;
};

// The physical connector standard of the on-board serial port. RS423 on the
// Model B / B+ / Master 128; RS232 on a Master Compact fitted with serial (only
// the signal levels differ -- it is purely a label). Static and machine-defined:
// a hardware/memory type may declare
//   static constexpr std::string_view serial_connector = "RS232";
// otherwise it defaults to "RS423". Orthogonal to HasSerialSocket (whether the
// port exists) and to the dynamic RS423/cassette select bit (which mode the
// shared ULA baud bits currently drive).
template<typename T>
concept HasSerialConnectorLabel = requires {
    { T::serial_connector } -> std::convertible_to<std::string_view>;
};

template<typename T>
constexpr std::string_view serial_connector_label() {
    if constexpr (HasSerialConnectorLabel<T>) {
        return T::serial_connector;
    } else {
        return "RS423";
    }
}

}  // namespace beebium
