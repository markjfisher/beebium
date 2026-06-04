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

#ifndef BEEBIUM_EXT_HOST_SERIAL_SERVICE_HPP
#define BEEBIUM_EXT_HOST_SERIAL_SERVICE_HPP

#include "HostSerialEndpoint.hpp"
#include "host_serial.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <string>

namespace beebium {

// gRPC service for the host-serial extension: a typed config API (query +
// re-point) parallel to the GUI ExtensionUi path. It forwards to the extension's
// HostSerialEndpoint, reading state via its thread-safe ui_snapshot() and
// re-pointing via request_reopen() (whose apply runs on the emulation thread),
// so this service thread never touches the live port directly.
class HostSerialServiceImpl final : public HostSerial::Service {
public:
    explicit HostSerialServiceImpl(serial::HostSerialEndpoint& endpoint)
        : endpoint_(endpoint) {}

    grpc::Status GetConfig(grpc::ServerContext* /*context*/,
                           const HostSerialGetConfigRequest* /*request*/,
                           HostSerialConfig* response) override {
        fill_config(*response, endpoint_.ui_snapshot());
        return grpc::Status::OK;
    }

    grpc::Status SetConfig(grpc::ServerContext* /*context*/,
                           const HostSerialSetConfigRequest* request,
                           HostSerialConfig* response) override {
        // Only "device" re-points are supported at runtime; a pty can only be
        // created at startup.
        if (request->has_mode() && request->mode() == "pty") {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "host-serial: switching to pty at runtime is not supported; "
                "restart with --host-serial mode=pty");
        }
        if (request->has_mode() && request->mode() != "device") {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "host-serial: mode must be 'device'");
        }

        // Diff against the current config: a field absent from the request keeps
        // its current value.
        const auto current = endpoint_.ui_snapshot();
        std::string path = request->has_path() ? request->path() : current.device_path;
        int baud = request->has_baud() ? static_cast<int>(request->baud())
                                       : current.baud;

        if (path.empty()) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "host-serial: a device path is required");
        }
        if (baud <= 0) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "host-serial: baud must be positive");
        }

        endpoint_.request_reopen(std::move(path), baud);

        // The re-point is applied on the next emulation tick; report the snapshot
        // as it stands now (the proto documents the eventual-consistency).
        fill_config(*response, endpoint_.ui_snapshot());
        return grpc::Status::OK;
    }

private:
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

#endif  // BEEBIUM_EXT_HOST_SERIAL_SERVICE_HPP
