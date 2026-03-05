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

#include "AunPacket.hpp"
#include "NetworkBackend.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace beebium {

// Information about a configured peer in the AUN peer table.
struct PeerInfo {
    uint8_t net;
    uint8_t stn;
    uint32_t ip_addr;   // Network byte order
    uint16_t port;      // Host byte order
};

// UDP transport backend implementing the AUN (Acorn Universal Networking) protocol.
//
// Sends and receives AUN-formatted UDP datagrams. Peer addresses are configured
// explicitly via add_peer() — there is no auto-discovery in this implementation.
//
// The constructor creates and binds a UDP socket to the specified local port.
// If socket creation or binding fails, the backend enters a disconnected state
// (is_connected() returns false, DCD goes high, NFS ROM reports "No clock").
//
// send_frame() and receive_frame() perform direct non-blocking socket I/O.
// receive_frame() uses select() with zero timeout to avoid blocking the
// emulation thread.
class AunBackend : public NetworkBackend {
public:
    // Create a UDP socket bound to the specified local port.
    // local_net/local_stn identify this station for populating received frame addressing.
    explicit AunBackend(uint8_t local_net, uint8_t local_stn,
                        uint16_t local_port = AUN_DEFAULT_PORT);

    ~AunBackend() override;

    // Non-copyable (owns socket fd)
    AunBackend(const AunBackend&) = delete;
    AunBackend& operator=(const AunBackend&) = delete;

    // --- NetworkBackend interface ---

    void send_frame(const NetworkFrame& frame) override;
    std::optional<NetworkFrame> receive_frame() override;
    bool is_connected() const override;

    // --- Peer management ---

    // Add a peer mapping: Econet address (net, stn) <-> UDP endpoint (ip_addr, port).
    // ip_addr is in network byte order. port is in host byte order.
    void add_peer(uint8_t net, uint8_t stn, uint32_t ip_addr, uint16_t port);

    // Remove a peer mapping by Econet address.
    void remove_peer(uint8_t net, uint8_t stn);

    // Number of configured peers.
    size_t peer_count() const;

    // The local UDP port this backend is bound to.
    uint16_t local_port() const;

    // The local network number for this station.
    uint8_t local_net() const;

    // Enumerate all configured peers.
    std::vector<PeerInfo> list_peers() const;

private:
    int socket_fd_ = -1;
    uint16_t local_port_;
    uint8_t local_net_;
    uint8_t local_stn_;
    bool connected_ = false;

    // Handle generation: incremented by 4 for each outgoing request.
    // For Ack/ImmReply, the handle from the most recently received packet is echoed.
    uint32_t next_handle_ = 0;
    uint32_t last_received_handle_ = 0;

    // Peer table: bidirectional mapping between Econet addresses and UDP endpoints.
    // Forward: (net << 8 | stn) -> (ip_addr, port)
    // Reverse: (ip_addr << 16 | port) -> (net, stn)
    std::unordered_map<uint16_t, std::pair<uint32_t, uint16_t>> forward_map_;
    std::unordered_map<uint64_t, std::pair<uint8_t, uint8_t>> reverse_map_;

    // Reusable receive buffer (avoids allocation per receive_frame call).
    std::array<uint8_t, 2048> recv_buffer_;

    // Key construction helpers.
    static uint16_t make_forward_key(uint8_t net, uint8_t stn);
    static uint64_t make_reverse_key(uint32_t ip_addr, uint16_t port);

    void close_socket();
};

}  // namespace beebium
