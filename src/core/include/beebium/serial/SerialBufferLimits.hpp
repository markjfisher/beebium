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

#include <cstddef>

namespace beebium::serial {

// Shared serial-device queue defaults, so the bounds live in one place rather
// than being duplicated across the device endpoints.

// Default transmit-queue back-pressure mark (bytes, Beeb -> device): at or above
// this the device reports !accepts_more(), so the Serial ULA drives the ACIA's
// /CTS input and the guest's transmit loop busy-waits. Overridable per extension
// via the tx_buffer parameter.
inline constexpr std::size_t kDefaultTxBackPressure = 4096;

// The absolute transmit-queue cap is the back-pressure mark plus this margin.
// Bytes beyond it are dropped, which is only reachable if the guest also ignores
// /CTS (a real ACIA loses data there too).
inline constexpr std::size_t kTxHardCapMargin = 64;

// Default receive-queue capacity (bytes, device -> Beeb).
inline constexpr std::size_t kDefaultRxCapacity = 4096;

}  // namespace beebium::serial
