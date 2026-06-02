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

#include "SerialSocket.hpp"

#include <concepts>

namespace beebium {

// Concept to detect if a hardware type has the on-board serial socket
// (MC6850 ACIA + Serial ULA). All current Model B variants provide this.
template<typename T>
concept HasSerialSocket = requires(T& hw) {
    { hw.serial_socket } -> std::same_as<SerialSocket&>;
};

}  // namespace beebium
