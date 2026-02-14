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
#include <optional>
#include <vector>

namespace beebium {

// Abstract network transport for the MC6854 ADLC.
//
// Decouples the ADLC from the underlying network transport (UDP/AUN, loopback, test
// double). The ADLC calls send() when a complete frame has been assembled from the TX
// FIFO, and polls receive() on each byte-trickle interval to check for incoming frames.
class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;

    // Send a complete frame (address bytes + control + data, no flags or CRC).
    virtual void send(const std::vector<uint8_t>& frame) = 0;

    // Non-blocking receive. Returns the next complete frame if one is available,
    // or std::nullopt if the receive queue is empty.
    virtual std::optional<std::vector<uint8_t>> receive() = 0;

    // Whether the network link is active (DCD sense: true = carrier present).
    virtual bool is_connected() const = 0;
};

}  // namespace beebium
