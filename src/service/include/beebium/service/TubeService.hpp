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

#ifndef BEEBIUM_SERVICE_TUBE_SERVICE_HPP
#define BEEBIUM_SERVICE_TUBE_SERVICE_HPP

#include "tube.grpc.pb.h"
#include "beebium/tube/TubeConcepts.hpp"
#include "beebium/tube/TubeSocket.hpp"

#include <grpcpp/grpcpp.h>
#include <mutex>
#include <string>

namespace beebium::service {

// TubeService implementation.
//
// Provides gRPC RPCs for querying Tube status and (future) state
// save/restore. The cross-process shared-memory infrastructure has been
// removed; Tube coprocessors now run in-process as extensions.

template<typename MachineType>
class TubeServiceImpl final : public TubeService::Service {
public:
    explicit TubeServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~TubeServiceImpl() override = default;

    TubeServiceImpl(const TubeServiceImpl&) = delete;
    TubeServiceImpl& operator=(const TubeServiceImpl&) = delete;

    // --- RPC implementations ---

    grpc::Status Connect(
        grpc::ServerContext* context,
        const TubeConnectRequest* request,
        TubeConnectResponse* response) override
    {
        (void)context;
        (void)request;
        (void)response;
        // Cross-process Connect is no longer supported. Tube coprocessors
        // are now loaded as in-process extensions.
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                            "Cross-process Tube connection removed; use extensions");
    }

    grpc::Status RegisterEndpoint(
        grpc::ServerContext* context,
        const RegisterEndpointRequest* request,
        RegisterEndpointResponse* response) override
    {
        (void)context;
        (void)request;
        (void)response;
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                            "Cross-process Tube connection removed; use extensions");
    }

    grpc::Status GetStatus(
        grpc::ServerContext* context,
        const GetTubeStatusRequest* request,
        GetTubeStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasTubeSocket<Memory>) {
            response->set_has_tube_socket(false);
            return grpc::Status::OK;
        } else {
            response->set_has_tube_socket(true);

            auto& tube = machine_.state().memory.tube_socket;
            response->set_enabled(tube.enabled());

            return grpc::Status::OK;
        }
    }

    grpc::Status GetState(
        grpc::ServerContext* context,
        const TubeGetStateRequest* request,
        TubeGetStateResponse* response) override
    {
        (void)context;
        (void)request;
        (void)response;
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                            "State serialisation not yet implemented");
    }

    grpc::Status RestoreState(
        grpc::ServerContext* context,
        const TubeRestoreStateRequest* request,
        TubeRestoreStateResponse* response) override
    {
        (void)context;
        (void)request;
        (void)response;
        return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                            "State restore not yet implemented");
    }

private:
    MachineType& machine_;
    std::mutex mutex_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_TUBE_SERVICE_HPP
