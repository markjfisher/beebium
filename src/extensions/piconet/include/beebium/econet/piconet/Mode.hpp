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

#include <cstdint>

namespace beebium::piconet {

// Operating modes for SET_MODE command. Underlying integer values match the
// firmware's PICONET_CMD_SET_MODE_* enumeration so they can be transmitted
// or compared to the STATUS event's mode field directly.
// Source: piconet/board/src/piconet.c lines 96-103.
enum class Mode : std::uint8_t {
    Stop    = 0,
    Listen  = 1,
    Monitor = 2,
};

}  // namespace beebium::piconet
