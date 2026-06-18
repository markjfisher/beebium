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

#ifndef BEEBIUM_EXT_RPC_SERIAL_DISPATCHER_HPP
#define BEEBIUM_EXT_RPC_SERIAL_DISPATCHER_HPP

#include "RpcSerialEndpoint.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "rpc_serial.pb.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the RpcSerial service. It parses the
// extension's own protobuf request messages from the opaque request bytes,
// drives the RpcSerialEndpoint, and serializes the reply -- the same logic the
// old RpcSerialServiceImpl had, minus any gRPC. The plugin therefore links
// protobuf (for these message types) but not gRPC, so there is one gRPC
// runtime in the process. See docs/discussion/extension-rpc-channel.md.
//
// When the typed-stub generator lands (Phase 2) this hand-written switch is
// what it will emit; until then it is a faithful, readable template.
class RpcSerialDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit RpcSerialDispatcher(RpcSerialEndpoint& endpoint)
        : endpoint_(endpoint) {}

    std::string_view service_name() const override { return "RpcSerial"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "Send") {
            RpcSerialSendRequest req;
            if (!parse(request, req)) {
                return bad_request("Send");
            }
            const std::string& data = req.data();
            const std::size_t accepted = endpoint_.inject(
                reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
            RpcSerialSendResponse resp;
            resp.set_accepted(static_cast<std::uint32_t>(accepted));
            resp.SerializeToString(&response);
            return RpcStatus::ok();
        }
        if (method == "Receive") {
            RpcSerialReceiveRequest req;
            if (!parse(request, req)) {
                return bad_request("Receive");
            }
            std::vector<std::uint8_t> bytes = endpoint_.drain(req.max_bytes());
            RpcSerialReceiveResponse resp;
            resp.set_data(std::string(bytes.begin(), bytes.end()));
            resp.SerializeToString(&response);
            return RpcStatus::ok();
        }
        if (method == "GetStatus") {
            RpcSerialStatus resp;
            resp.set_tx_pending(static_cast<std::uint32_t>(endpoint_.tx_pending()));
            resp.set_rx_pending(static_cast<std::uint32_t>(endpoint_.rx_pending()));
            resp.SerializeToString(&response);
            return RpcStatus::ok();
        }
        return RpcStatus::error(kRpcUnimplemented,
                                "RpcSerial has no method '" + std::string(method) + "'");
    }

private:
    template <typename Msg>
    static bool parse(std::string_view bytes, Msg& out) {
        return out.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    static RpcStatus bad_request(const char* method) {
        return RpcStatus::error(kRpcInvalidArgument,
                                std::string("malformed RpcSerial.") + method + " request");
    }

    RpcSerialEndpoint& endpoint_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_RPC_SERIAL_DISPATCHER_HPP
