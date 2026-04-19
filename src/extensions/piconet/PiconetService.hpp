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

#ifndef BEEBIUM_EXTENSIONS_PICONET_PICONET_SERVICE_HPP
#define BEEBIUM_EXTENSIONS_PICONET_PICONET_SERVICE_HPP

// gRPC service for Piconet-specific status and (eventually) control.
// Owned by PiconetEconetTransportExtension and surfaced via its
// grpc_services() hook; the gRPC server only exposes these RPCs when
// Piconet is the active transport.
//
// Initial scope is intentionally minimal -- see piconet_service.proto
// for the design intent and the list of future RPCs to add as user
// needs arise.

#include "piconet_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

namespace beebium {

class PiconetEconetTransportExtension;

class PiconetServiceImpl final : public PiconetService::Service {
public:
    explicit PiconetServiceImpl(PiconetEconetTransportExtension& extension);

    grpc::Status GetStatus(grpc::ServerContext* context,
                           const PiconetGetStatusRequest* request,
                           PiconetGetStatusResponse* response) override;

private:
    PiconetEconetTransportExtension& extension_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_PICONET_PICONET_SERVICE_HPP
