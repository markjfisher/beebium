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

// Platform-agnostic alias for the PTY-bridged (POSIX) or named-pipe-
// bridged (Windows) FakePiconetDevice. Tests that want to run on both
// platforms include this header and construct
// beebium::piconet::test::FakePiconetDeviceOnSerial without caring about
// which underlying bridge is in use; both expose the same public API
// (is_open, slave_path, device).

#ifdef _WIN32
#include "FakePiconetDeviceOnPipe.hpp"
#else
#include "FakePiconetDeviceOnPty.hpp"
#endif

namespace beebium::piconet::test {

#ifdef _WIN32
using FakePiconetDeviceOnSerial = FakePiconetDeviceOnPipe;
#else
using FakePiconetDeviceOnSerial = FakePiconetDeviceOnPty;
#endif

}  // namespace beebium::piconet::test
