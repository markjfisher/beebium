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

#include "beebium/econet/piconet/Constants.hpp"

#include <cstdint>
#include <string>

namespace beebium::piconet {

// Configuration for the Piconet backend, populated from CLI args or preset
// JSON before the backend is constructed. Lives in its own header so that
// ServerMain and PresetLoader can reference the type without pulling in
// PiconetBackend's heavier transitive includes (thread, atomic, termios).
struct PiconetConfig {
    std::string  device_path;                       // POSIX device path, e.g. /dev/tty.usbmodem101
    std::uint8_t initial_station = DEFAULT_STATION; // Station to send to the device at startup
};

}  // namespace beebium::piconet
