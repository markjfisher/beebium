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

#include "EconetSocket.hpp"

#include <concepts>

namespace beebium {

// Concept to detect if a hardware type has an Econet socket.
// All current hardware variants (Model B, Model B+, Model B with ROM/RAM board) have this.
template<typename T>
concept HasEconetSocket = requires(T& hw) {
    { hw.econet_socket } -> std::same_as<EconetSocket&>;
};

// Helper to check if Econet hardware is fitted at runtime.
// Returns true only if the hardware has an EconetSocket AND it is enabled.
template<typename T>
bool econet_present(const T& hw) {
    if constexpr (HasEconetSocket<T>) {
        return hw.econet_socket.enabled();
    } else {
        return false;
    }
}

}  // namespace beebium
