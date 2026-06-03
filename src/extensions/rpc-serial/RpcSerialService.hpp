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

#ifndef BEEBIUM_EXT_RPC_SERIAL_SERVICE_HPP
#define BEEBIUM_EXT_RPC_SERIAL_SERVICE_HPP

#include "rpc_serial.grpc.pb.h"
#include <beebium/serial/SerialDevice.hpp>

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <string>
#include <vector>

namespace beebium {

// gRPC service for the rpc-serial extension: the client is the serial device.
// It forwards directly to the extension's ScriptableSerialEndpoint, whose
// queues are mutex-protected, so the emulation thread and this service thread
// can use it concurrently without a machine pause.
class RpcSerialServiceImpl final : public RpcSerial::Service {
public:
    explicit RpcSerialServiceImpl(ScriptableSerialEndpoint& endpoint)
        : endpoint_(endpoint) {}

    grpc::Status Send(grpc::ServerContext* /*context*/,
                      const RpcSerialSendRequest* request,
                      RpcSerialSendResponse* response) override {
        const std::string& data = request->data();
        const std::size_t accepted = endpoint_.inject(
            reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        response->set_accepted(static_cast<std::uint32_t>(accepted));
        return grpc::Status::OK;
    }

    grpc::Status Receive(grpc::ServerContext* /*context*/,
                         const RpcSerialReceiveRequest* request,
                         RpcSerialReceiveResponse* response) override {
        std::vector<std::uint8_t> bytes = endpoint_.drain(request->max_bytes());
        response->set_data(std::string(bytes.begin(), bytes.end()));
        return grpc::Status::OK;
    }

    grpc::Status GetStatus(grpc::ServerContext* /*context*/,
                           const RpcSerialStatusRequest* /*request*/,
                           RpcSerialStatus* response) override {
        response->set_tx_pending(static_cast<std::uint32_t>(endpoint_.tx_pending()));
        response->set_rx_pending(static_cast<std::uint32_t>(endpoint_.rx_pending()));
        return grpc::Status::OK;
    }

private:
    ScriptableSerialEndpoint& endpoint_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_RPC_SERIAL_SERVICE_HPP
