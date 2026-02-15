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

#include <beebium/econet/AunBackend.hpp>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <span>

namespace beebium {

AunBackend::AunBackend(uint8_t local_net, uint8_t local_stn, uint16_t local_port)
    : local_port_(local_port), local_net_(local_net), local_stn_(local_stn) {

    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "AunBackend: socket() failed: " << std::strerror(errno) << "\n";
        return;
    }

    // Allow rapid restart — avoids "Address already in use" after a crash.
    int reuse = 1;
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "AunBackend: SO_REUSEADDR failed: " << std::strerror(errno) << "\n";
        close_socket();
        return;
    }

    // Enable broadcast sends (for sending to all known peers).
    int broadcast = 1;
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        std::cerr << "AunBackend: SO_BROADCAST failed: " << std::strerror(errno) << "\n";
        close_socket();
        return;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(local_port);

    if (::bind(socket_fd_, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        std::cerr << "AunBackend: bind() to port " << local_port
                  << " failed: " << std::strerror(errno) << "\n";
        close_socket();
        return;
    }

    connected_ = true;
}

AunBackend::~AunBackend() {
    close_socket();
}

void AunBackend::send_frame(const NetworkFrame& frame) {
    if (socket_fd_ < 0) return;

    // Choose handle: echo for Ack/ImmReply, increment for everything else.
    uint32_t handle;
    if (frame.type == FrameType::Ack || frame.type == FrameType::ImmReply) {
        handle = last_received_handle_;
    } else {
        next_handle_ += 4;
        handle = next_handle_;
    }

    auto packet = aun_packet::encode(frame, handle);

    if (frame.type == FrameType::Broadcast) {
        // Send broadcast to every known peer.
        for (const auto& [key, endpoint] : forward_map_) {
            sockaddr_in dest_addr{};
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_addr.s_addr = endpoint.first;
            dest_addr.sin_port = htons(endpoint.second);

            auto sent = ::sendto(socket_fd_, packet.data(), packet.size(), 0,
                                 reinterpret_cast<const sockaddr*>(&dest_addr),
                                 sizeof(dest_addr));
            if (sent < 0) {
                std::cerr << "AunBackend: sendto() broadcast failed: "
                          << std::strerror(errno) << "\n";
            }
        }
        return;
    }

    // Unicast / Ack / Immediate / ImmReply / Nack: look up destination peer.
    auto forward_key = make_forward_key(frame.dest_net, frame.dest_stn);
    auto it = forward_map_.find(forward_key);
    if (it == forward_map_.end()) {
        // Unknown peer — drop silently.
        return;
    }

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = it->second.first;
    dest_addr.sin_port = htons(it->second.second);

    auto sent = ::sendto(socket_fd_, packet.data(), packet.size(), 0,
                         reinterpret_cast<const sockaddr*>(&dest_addr),
                         sizeof(dest_addr));
    if (sent < 0) {
        std::cerr << "AunBackend: sendto() failed: " << std::strerror(errno) << "\n";
    }
}

std::optional<NetworkFrame> AunBackend::receive_frame() {
    if (socket_fd_ < 0) return std::nullopt;

    // Non-blocking check: is there data waiting?
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_fd_, &readfds);

    timeval timeout{};  // Zero timeout = non-blocking poll
    int ready = ::select(socket_fd_ + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready <= 0) return std::nullopt;

    // Read the datagram and capture the sender's address.
    sockaddr_in sender_addr{};
    socklen_t sender_len = sizeof(sender_addr);

    auto received = ::recvfrom(socket_fd_, recv_buffer_.data(), recv_buffer_.size(), 0,
                               reinterpret_cast<sockaddr*>(&sender_addr), &sender_len);
    if (received < 0) {
        std::cerr << "AunBackend: recvfrom() failed: " << std::strerror(errno) << "\n";
        return std::nullopt;
    }

    if (received < static_cast<ssize_t>(AUN_HEADER_SIZE)) {
        return std::nullopt;  // Too short — discard
    }

    // Skip self-sends (our own broadcasts looping back).
    uint32_t sender_ip = sender_addr.sin_addr.s_addr;
    uint16_t sender_port = ntohs(sender_addr.sin_port);
    if (sender_port == local_port_) {
        // Check if the source IP is any of our local addresses.
        // On loopback, the source will be 127.0.0.1 which won't match INADDR_ANY,
        // but self-sends are still possible if we send to a peer on the same port.
        // This is a simple heuristic — if the port matches, check the reverse map.
        // If the sender IS in our peer table, it's a real peer, not us.
        auto reverse_key = make_reverse_key(sender_ip, sender_port);
        if (reverse_map_.find(reverse_key) == reverse_map_.end()) {
            return std::nullopt;  // Unknown sender on our port — likely self
        }
    }

    auto result = aun_packet::decode(
        std::span<const uint8_t>(recv_buffer_.data(), static_cast<size_t>(received)));
    if (!result.valid) {
        return std::nullopt;
    }

    // Look up sender in reverse peer table.
    auto reverse_key = make_reverse_key(sender_ip, sender_port);
    auto it = reverse_map_.find(reverse_key);
    if (it == reverse_map_.end()) {
        return std::nullopt;  // Unknown peer — discard
    }

    // Populate addressing from peer table.
    result.frame.src_net = it->second.first;
    result.frame.src_stn = it->second.second;
    result.frame.dest_net = local_net_;
    result.frame.dest_stn = local_stn_;

    last_received_handle_ = result.handle;

    return std::move(result.frame);
}

bool AunBackend::is_connected() const {
    return connected_;
}

void AunBackend::add_peer(uint8_t net, uint8_t stn, uint32_t ip_addr, uint16_t port) {
    auto fwd_key = make_forward_key(net, stn);
    forward_map_[fwd_key] = {ip_addr, port};

    auto rev_key = make_reverse_key(ip_addr, port);
    reverse_map_[rev_key] = {net, stn};
}

void AunBackend::remove_peer(uint8_t net, uint8_t stn) {
    auto fwd_key = make_forward_key(net, stn);
    auto it = forward_map_.find(fwd_key);
    if (it != forward_map_.end()) {
        auto rev_key = make_reverse_key(it->second.first, it->second.second);
        reverse_map_.erase(rev_key);
        forward_map_.erase(it);
    }
}

size_t AunBackend::peer_count() const {
    return forward_map_.size();
}

uint16_t AunBackend::local_port() const {
    return local_port_;
}

uint16_t AunBackend::make_forward_key(uint8_t net, uint8_t stn) {
    return (static_cast<uint16_t>(net) << 8) | stn;
}

uint64_t AunBackend::make_reverse_key(uint32_t ip_addr, uint16_t port) {
    return (static_cast<uint64_t>(ip_addr) << 16) | port;
}

void AunBackend::close_socket() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
}

}  // namespace beebium
