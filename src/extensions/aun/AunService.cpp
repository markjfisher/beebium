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

#include "AunService.hpp"

#include "AunEconetTransportExtension.hpp"
#include "beebium/econet/AunBackend.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <cstring>

namespace beebium {

namespace {

grpc::Status set_failure(google::protobuf::Message* response, const char* error) {
    // The four success/error responses share an identical
    // {bool success, string error} layout; populate via reflection so we
    // don't have to template this.
    auto* refl = response->GetReflection();
    auto* desc = response->GetDescriptor();
    refl->SetBool(response, desc->FindFieldByName("success"), false);
    refl->SetString(response, desc->FindFieldByName("error"), error);
    return grpc::Status::OK;
}

grpc::Status set_success(google::protobuf::Message* response) {
    auto* refl = response->GetReflection();
    auto* desc = response->GetDescriptor();
    refl->SetBool(response, desc->FindFieldByName("success"), true);
    return grpc::Status::OK;
}

std::string ip_to_dotted(std::uint32_t ip_net_byte_order) {
    char buf[INET_ADDRSTRLEN];
    in_addr addr;
    addr.s_addr = ip_net_byte_order;
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
        return buf;
    }
    return {};
}

}  // namespace

AunServiceImpl::AunServiceImpl(AunEconetTransportExtension& extension)
    : extension_(extension) {}

grpc::Status AunServiceImpl::SetConnected(
        grpc::ServerContext*,
        const AunSetConnectedRequest* request,
        AunSetConnectedResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* backend = extension_.backend();
    if (!backend) {
        return set_failure(response, "AUN backend is not active");
    }
    backend->set_connected(request->connected());
    return set_success(response);
}

grpc::Status AunServiceImpl::AddPeer(
        grpc::ServerContext*,
        const AunAddPeerRequest* request,
        AunAddPeerResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* backend = extension_.backend();
    if (!backend) {
        return set_failure(response, "AUN backend is not active");
    }

    if (request->net() > 0xFF) {
        return set_failure(response, "net must be 0-255");
    }
    if (request->stn() < 1 || request->stn() > 254) {
        return set_failure(response, "stn must be 1-254");
    }

    in_addr addr{};
    if (inet_pton(AF_INET, request->ip_address().c_str(), &addr) != 1) {
        return set_failure(response, "invalid ip_address");
    }

    std::uint16_t port = (request->port() == 0)
        ? AUN_DEFAULT_PORT
        : static_cast<std::uint16_t>(request->port());

    backend->add_peer(static_cast<std::uint8_t>(request->net()),
                      static_cast<std::uint8_t>(request->stn()),
                      addr.s_addr,
                      port);
    return set_success(response);
}

grpc::Status AunServiceImpl::RemovePeer(
        grpc::ServerContext*,
        const AunRemovePeerRequest* request,
        AunRemovePeerResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* backend = extension_.backend();
    if (!backend) {
        return set_failure(response, "AUN backend is not active");
    }
    backend->remove_peer(static_cast<std::uint8_t>(request->net()),
                         static_cast<std::uint8_t>(request->stn()));
    return set_success(response);
}

grpc::Status AunServiceImpl::ListPeers(
        grpc::ServerContext*,
        const AunListPeersRequest*,
        AunListPeersResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* backend = extension_.backend();
    if (!backend) {
        return grpc::Status::OK;  // empty list when no backend
    }
    for (const auto& info : backend->list_peers()) {
        auto* peer = response->add_peers();
        peer->set_net(info.net);
        peer->set_stn(info.stn);
        peer->set_ip_address(ip_to_dotted(info.ip_addr));
        peer->set_port(info.port);
        peer->set_source(info.source == PeerSource::Discovered
                         ? AUN_PEER_SOURCE_DISCOVERED
                         : AUN_PEER_SOURCE_OPERATOR_CONFIGURED);
    }
    return grpc::Status::OK;
}

grpc::Status AunServiceImpl::GetStatus(
        grpc::ServerContext*,
        const AunGetStatusRequest*,
        AunGetStatusResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* backend = extension_.backend();
    if (!backend) {
        return grpc::Status::OK;  // all-zero defaults
    }
    response->set_connected(backend->is_connected());
    response->set_local_port(backend->local_port());
    response->set_peer_count(static_cast<uint32_t>(backend->peer_count()));
    return grpc::Status::OK;
}

}  // namespace beebium
