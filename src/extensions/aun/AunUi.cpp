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

#include "AunUi.hpp"

#include "AunEconetTransportExtension.hpp"
#include "beebium/econet/AunBackend.hpp"

#include "extension_ui.pb.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <string>

namespace beebium {

namespace {

constexpr const char* CONTROL_PEERS_GROUP   = "peers_group";
constexpr const char* CONTROL_NO_PEERS      = "no_peers";

// Format an IPv4 address (in network byte order, as carried in PeerInfo)
// to dotted-quad. Mirrors the helper in EconetService.hpp's anonymous
// namespace; reproduced here so AunUi doesn't depend on the service
// layer.
std::string format_ip(uint32_t ip_addr) {
    char buf[INET_ADDRSTRLEN];
    in_addr addr;
    addr.s_addr = ip_addr;
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
        return buf;
    }
    return "0.0.0.0";
}

// Compose the per-peer human-readable label, e.g. "0.254  127.0.0.1:32768".
// Two spaces between the Econet address and the IP endpoint matches the
// hardcoded SwiftUI it replaces.
std::string format_peer_line(const PeerInfo& peer) {
    std::string out;
    out += std::to_string(static_cast<unsigned>(peer.net));
    out += '.';
    out += std::to_string(static_cast<unsigned>(peer.stn));
    out += "  ";
    out += format_ip(peer.ip_addr);
    out += ':';
    out += std::to_string(peer.port);
    return out;
}

}  // namespace

void AunUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* root_group = root->mutable_group();
    root_group->set_label("AUN");

    // Slice 1: peers list only. Slice 2 will prepend a Connect/Disconnect
    // Button and a "Listening on UDP port N" Label here.
    auto* peers_control = root_group->add_controls();
    peers_control->set_id(CONTROL_PEERS_GROUP);
    auto* peers_group = peers_control->mutable_group();
    peers_group->set_label("Peers");

    AunBackend* backend = ext_.backend();
    if (!backend) {
        // Either port=none or socket bind failed. Nothing to list.
        auto* no_peers = peers_group->add_controls();
        no_peers->set_id(CONTROL_NO_PEERS);
        no_peers->mutable_label()->set_text("AUN backend unavailable");
        return;
    }

    auto peers = backend->list_peers();
    if (peers.empty()) {
        auto* no_peers = peers_group->add_controls();
        no_peers->set_id(CONTROL_NO_PEERS);
        no_peers->mutable_label()->set_text("No peer stations configured");
        return;
    }

    // Stable per-peer ids ("peer.<net>.<stn>") so SwiftUI patches in
    // place if the same peer is updated, and the renderer doesn't
    // rebuild the whole list each push.
    for (const auto& peer : peers) {
        auto* control = peers_group->add_controls();
        control->set_id("peer." + std::to_string(static_cast<unsigned>(peer.net)) +
                        "." + std::to_string(static_cast<unsigned>(peer.stn)));
        control->mutable_label()->set_text(format_peer_line(peer));
    }
}

void AunUi::handle_event(const DispatchRequest& /*request*/) {
    // No interactive controls in Slice 1 -- the panel is read-only.
    // Slice 2 will handle the Connect/Disconnect button; Slice 3 the
    // Add Peer form.
}

}  // namespace beebium
