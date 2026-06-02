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

// Compatibility shim: the canonical SerialPort interface now lives in
// beebium core (beebium::serial). The Piconet code keeps using the
// beebium::piconet names via these aliases. See src/core/include/beebium/serial/.

#include "beebium/serial/SerialPort.hpp"

namespace beebium::piconet {
using beebium::serial::SerialPort;
using beebium::serial::ReadResult;
using beebium::serial::WriteResult;
}  // namespace beebium::piconet
