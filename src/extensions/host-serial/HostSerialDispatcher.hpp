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

#ifndef BEEBIUM_EXT_HOST_SERIAL_DISPATCHER_HPP
#define BEEBIUM_EXT_HOST_SERIAL_DISPATCHER_HPP

#include "HostSerialEndpoint.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "host_serial.pb.h"

#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the HostSerial service: the typed
// config API (query + re-point) parallel to the GUI ExtensionUi path. It parses
// the extension's own protobuf request messages from the opaque request bytes,
// reads state via the endpoint's thread-safe ui_snapshot() and re-points via
// request_reopen() (whose apply runs on the emulation thread), and serializes
// the reply -- the same logic the old HostSerialServiceImpl had, minus any
// gRPC. The host-serial library therefore links protobuf (for these message
// types) but not gRPC. See docs/discussion/extension-rpc-channel.md.
class HostSerialDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit HostSerialDispatcher(serial::HostSerialEndpoint& endpoint)
        : endpoint_(endpoint) {}

    std::string_view service_name() const override { return "HostSerial"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetConfig") {
            HostSerialGetConfigRequest req;
            if (!parse(request, req)) {
                return bad_request("GetConfig");
            }
            HostSerialConfig resp;
            fill_config(resp, endpoint_.ui_snapshot());
            resp.SerializeToString(&response);
            return RpcStatus::ok();
        }
        if (method == "SetConfig") {
            HostSerialSetConfigRequest req;
            if (!parse(request, req)) {
                return bad_request("SetConfig");
            }
            return set_config(req, response);
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "HostSerial has no method '" + std::string(method) + "'");
    }

private:
    RpcStatus set_config(const HostSerialSetConfigRequest& request,
                         std::string& response) {
        // Only "device" re-points are supported at runtime; a pty can only be
        // created at startup.
        if (request.has_mode() && request.mode() == "pty") {
            return RpcStatus::error(
                kRpcInvalidArgument,
                "host-serial: switching to pty at runtime is not supported; "
                "restart with --host-serial mode=pty");
        }
        if (request.has_mode() && request.mode() != "device") {
            return RpcStatus::error(kRpcInvalidArgument,
                                    "host-serial: mode must be 'device'");
        }

        // Diff against the current config: a field absent from the request keeps
        // its current value.
        const auto current = endpoint_.ui_snapshot();
        std::string path =
            request.has_path() ? request.path() : current.device_path;
        int baud =
            request.has_baud() ? static_cast<int>(request.baud()) : current.baud;

        if (path.empty()) {
            return RpcStatus::error(kRpcInvalidArgument,
                                    "host-serial: a device path is required");
        }
        if (baud <= 0) {
            return RpcStatus::error(kRpcInvalidArgument,
                                    "host-serial: baud must be positive");
        }

        endpoint_.request_reopen(std::move(path), baud);

        // The re-point is applied on the next emulation tick; report the snapshot
        // as it stands now (the proto documents the eventual-consistency).
        HostSerialConfig resp;
        fill_config(resp, endpoint_.ui_snapshot());
        resp.SerializeToString(&response);
        return RpcStatus::ok();
    }

    template <typename Msg>
    static bool parse(std::string_view bytes, Msg& out) {
        return out.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    static RpcStatus bad_request(const char* method) {
        return RpcStatus::error(
            kRpcInvalidArgument,
            std::string("malformed HostSerial.") + method + " request");
    }
    static void fill_config(HostSerialConfig& out,
                            const serial::HostSerialEndpoint::UiSnapshot& snap) {
        out.set_mode(snap.mode);
        out.set_path(snap.device_path);
        out.set_baud(static_cast<std::uint32_t>(snap.baud));
        out.set_serial_open(snap.serial_open);
        out.set_open_error(snap.open_error_message);
    }

    serial::HostSerialEndpoint& endpoint_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_HOST_SERIAL_DISPATCHER_HPP
