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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_DISPATCHER_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_DISPATCHER_HPP

// Hand-written ExtensionRpcDispatcher for AUN-specific operations (peer table
// management, cable-plug simulation, port reporting), served through the core's
// ExtensionRpc channel rather than a plugin-hosted gRPC service. The aun
// library therefore links protobuf (for these messages) but not gRPC. See
// docs/discussion/extension-rpc-channel.md.
//
// Validation and "backend not active" conditions are reported in-band (the
// response's success=false + error string, with an OK RpcStatus), exactly as
// the old AunServiceImpl did -- clients check response.success, not the
// transport status. A request that is not valid protobuf is the one case that
// maps to a non-OK RpcStatus (kRpcInvalidArgument).

#include "AunEconetTransportExtension.hpp"
#include "beebium/econet/AunBackend.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "aun.pb.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <mutex>
#include <string>
#include <string_view>

namespace beebium {

class AunDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit AunDispatcher(AunEconetTransportExtension& extension)
        : extension_(extension) {}

    std::string_view service_name() const override { return "AunService"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "SetConnected") {
            return handle<AunSetConnectedRequest, AunSetConnectedResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    auto* backend = extension_.backend();
                    if (!backend) {
                        return fail(resp, "AUN backend is not active");
                    }
                    backend->set_connected(req.connected());
                    resp.set_success(true);
                });
        }
        if (method == "AddPeer") {
            return handle<AunAddPeerRequest, AunAddPeerResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    add_peer(req, resp);
                });
        }
        if (method == "RemovePeer") {
            return handle<AunRemovePeerRequest, AunRemovePeerResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    auto* backend = extension_.backend();
                    if (!backend) {
                        return fail(resp, "AUN backend is not active");
                    }
                    backend->remove_peer(static_cast<std::uint8_t>(req.net()),
                                         static_cast<std::uint8_t>(req.stn()));
                    resp.set_success(true);
                });
        }
        if (method == "ListPeers") {
            return handle<AunListPeersRequest, AunListPeersResponse>(
                method, request, response, [&](const auto&, auto& resp) {
                    list_peers(resp);
                });
        }
        if (method == "GetStatus") {
            return handle<AunGetStatusRequest, AunGetStatusResponse>(
                method, request, response, [&](const auto&, auto& resp) {
                    auto* backend = extension_.backend();
                    if (!backend) {
                        return;  // all-zero defaults
                    }
                    resp.set_connected(backend->is_connected());
                    resp.set_local_port(backend->local_port());
                    resp.set_peer_count(
                        static_cast<std::uint32_t>(backend->peer_count()));
                });
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "AunService has no method '" + std::string(method) + "'");
    }

private:
    // Parse the request, run `body` under the lock, serialize the response.
    template <typename Req, typename Resp, typename Body>
    RpcStatus handle(std::string_view method, std::string_view request,
                     std::string& response, Body&& body) {
        Req req;
        if (!req.ParseFromArray(request.data(),
                                static_cast<int>(request.size()))) {
            return RpcStatus::error(
                kRpcInvalidArgument,
                "malformed AunService." + std::string(method) + " request");
        }
        Resp resp;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            body(req, resp);
        }
        resp.SerializeToString(&response);
        return RpcStatus::ok();
    }

    template <typename Resp>
    static void fail(Resp& resp, const char* error) {
        resp.set_success(false);
        resp.set_error(error);
    }

    void add_peer(const AunAddPeerRequest& req, AunAddPeerResponse& resp) {
        auto* backend = extension_.backend();
        if (!backend) {
            return fail(resp, "AUN backend is not active");
        }
        if (req.net() > 0xFF) {
            return fail(resp, "net must be 0-255");
        }
        if (req.stn() < 1 || req.stn() > 254) {
            return fail(resp, "stn must be 1-254");
        }
        in_addr addr{};
        if (inet_pton(AF_INET, req.ip_address().c_str(), &addr) != 1) {
            return fail(resp, "invalid ip_address");
        }
        std::uint16_t port = (req.port() == 0)
            ? AUN_DEFAULT_PORT
            : static_cast<std::uint16_t>(req.port());
        backend->add_peer(static_cast<std::uint8_t>(req.net()),
                          static_cast<std::uint8_t>(req.stn()),
                          addr.s_addr, port);
        resp.set_success(true);
    }

    void list_peers(AunListPeersResponse& resp) {
        auto* backend = extension_.backend();
        if (!backend) {
            return;  // empty list when no backend
        }
        for (const auto& info : backend->list_peers()) {
            auto* peer = resp.add_peers();
            peer->set_net(info.net);
            peer->set_stn(info.stn);
            peer->set_ip_address(ip_to_dotted(info.ip_addr));
            peer->set_port(info.port);
            peer->set_source(info.source == PeerSource::Discovered
                             ? AUN_PEER_SOURCE_DISCOVERED
                             : AUN_PEER_SOURCE_OPERATOR_CONFIGURED);
        }
    }

    static std::string ip_to_dotted(std::uint32_t ip_net_byte_order) {
        char buf[INET_ADDRSTRLEN];
        in_addr addr;
        addr.s_addr = ip_net_byte_order;
        if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
            return buf;
        }
        return {};
    }

    AunEconetTransportExtension& extension_;
    std::mutex mutex_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_DISPATCHER_HPP
