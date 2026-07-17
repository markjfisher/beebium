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

#ifndef BEEBIUM_EXTENSIONS_PICONET_PICONET_DISPATCHER_HPP
#define BEEBIUM_EXTENSIONS_PICONET_PICONET_DISPATCHER_HPP

#include "PiconetEconetTransportExtension.hpp"
#include "beebium/econet/PiconetBackend.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "piconet_service.pb.h"

#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the PiconetService: Piconet-specific
// status, served through the core's ExtensionRpc channel rather than a
// plugin-hosted gRPC service. The piconet plugin therefore links protobuf (for
// these messages) but not gRPC -- which is what kept it stable on Windows (the
// duplicate static gRPC runtime that crashed the streaming path is gone, and
// can't come back). See docs/discussion/extension-rpc-channel.md and
// docs/discussion/grpc-windows-streaming-race.md.
class PiconetDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit PiconetDispatcher(PiconetEconetTransportExtension& extension)
        : extension_(extension) {}

    std::string_view service_name() const override { return "PiconetService"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetStatus") {
            PiconetGetStatusRequest req;
            if (!req.ParseFromArray(request.data(),
                                    static_cast<int>(request.size()))) {
                return RpcStatus::error(kRpcInvalidArgument,
                                        "malformed PiconetService.GetStatus request");
            }
            PiconetGetStatusResponse resp;
            const PiconetBackend* backend = extension_.backend();
            if (backend == nullptr) {
                // Extension loaded but the backend never came up (device path
                // missing or open failed): empty path, closed.
                resp.set_device_path(std::string{});
                resp.set_serial_open(false);
            } else {
                resp.set_device_path(backend->config().device_path);
                // serial_open is the USB physical-layer state -- whether the
                // adapter is reachable at all (distinct from is_connected(),
                // which also requires firmware mode == LISTEN).
                resp.set_serial_open(backend->is_serial_open());
            }
            return serialized(resp.SerializeToString(&response));
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "PiconetService has no method '" + std::string(method) + "'");
    }

private:
    PiconetEconetTransportExtension& extension_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_PICONET_PICONET_DISPATCHER_HPP
