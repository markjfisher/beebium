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

// AUN packet types for typed frame transport between the ADLC and network backend.
// Values match the AUN wire format.
enum class FrameType : uint8_t {
    RawFrame   = 0,  // Raw Econet frame (non-AUN passthrough)
    Broadcast  = 1,  // AUN broadcast (fire-and-forget)
    Unicast    = 2,  // AUN unicast data transfer
    Ack        = 3,  // AUN acknowledgement
    Nack       = 4,  // AUN negative acknowledgement
    Immediate  = 5,  // AUN immediate operation request
    ImmReply   = 6,  // AUN immediate operation reply
};

// A typed network frame carrying Econet/AUN addressing and payload.
//
// For RawFrame: data contains the complete Econet frame bytes (address + control + data).
// For typed AUN frames: addressing is in the header fields, data contains the payload
// after the control byte and port.
struct NetworkFrame {
    FrameType type = FrameType::RawFrame;
    uint8_t port = 0;
    uint8_t control_byte = 0;
    uint8_t dest_net = 0;
    uint8_t dest_stn = 0;
    uint8_t src_net = 0;
    uint8_t src_stn = 0;
    std::vector<uint8_t> data;
};

// Abstract network transport for the MC6854 ADLC.
//
// Decouples the ADLC from the underlying network transport (UDP/AUN, loopback, test
// double). In the emulation inner loop, send_frame() and receive_frame() must be fast
// queue operations — no system calls. The real AunBackend (Phase 7) uses a separate
// I/O thread for UDP socket operations, with lock-free queues bridging the two threads.
class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;

    // Send a complete frame with type metadata.
    // For RawFrame, data contains the full Econet frame bytes.
    // For AUN types, header fields carry addressing and data carries the payload.
    virtual void send_frame(const NetworkFrame& frame) = 0;

    // Non-blocking receive. Returns the next complete frame if one is available,
    // or std::nullopt if the receive queue is empty.
    virtual std::optional<NetworkFrame> receive_frame() = 0;

    // Whether the network link is active (DCD sense: true = carrier present).
    virtual bool is_connected() const = 0;

    // Whether the network is currently receiving flag sequences (inter-frame fill).
    // The ADLC uses this to drive Flag Detected (SR1b3) and suppress Rx Idle.
    // On real hardware this comes from the physical line; in emulation, the
    // four-way handshake bridge generates synthetic flag fill during transactions.
    virtual bool is_receiving_flags() const { return false; }
};

}  // namespace beebium
