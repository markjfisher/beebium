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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_SERVICE_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_SERVICE_HPP

// gRPC service for AUN-specific operations: peer table management,
// cable-plug simulation, and AUN-port reporting. Owned by
// AunEconetTransportExtension and surfaced via its grpc_services()
// hook so the gRPC server only exposes these RPCs when AUN is the
// active transport. EconetService remains transport-agnostic.

#include "aun.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <mutex>

namespace beebium {

class AunEconetTransportExtension;

class AunServiceImpl final : public AunService::Service {
public:
    explicit AunServiceImpl(AunEconetTransportExtension& extension);

    grpc::Status SetConnected(grpc::ServerContext* context,
                              const AunSetConnectedRequest* request,
                              AunSetConnectedResponse* response) override;

    grpc::Status AddPeer(grpc::ServerContext* context,
                         const AunAddPeerRequest* request,
                         AunAddPeerResponse* response) override;

    grpc::Status RemovePeer(grpc::ServerContext* context,
                            const AunRemovePeerRequest* request,
                            AunRemovePeerResponse* response) override;

    grpc::Status ListPeers(grpc::ServerContext* context,
                           const AunListPeersRequest* request,
                           AunListPeersResponse* response) override;

    grpc::Status GetStatus(grpc::ServerContext* context,
                           const AunGetStatusRequest* request,
                           AunGetStatusResponse* response) override;

private:
    AunEconetTransportExtension& extension_;
    std::mutex mutex_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_SERVICE_HPP
