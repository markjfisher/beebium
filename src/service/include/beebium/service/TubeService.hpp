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
#include "beebium/tube/TubeSharedMemory.hpp"

#include <grpcpp/grpcpp.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace beebium::service {

// TubeService implementation.
//
// Manages the host-side Tube socket and shared memory region. The parasite
// process calls Connect to learn the shared memory name; the host uses
// GetState/RestoreState for save/restore of parasite state.
//
// The service does not own the TubeSharedMemory -- it holds a non-owning
// pointer set by the host startup code via set_shared_memory(). This allows
// the service to be constructed before shared memory exists (it is created
// during install_tube(), after the service object is built).

template<typename MachineType>
class TubeServiceImpl final : public TubeService::Service {
public:
    explicit TubeServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~TubeServiceImpl() override = default;

    TubeServiceImpl(const TubeServiceImpl&) = delete;
    TubeServiceImpl& operator=(const TubeServiceImpl&) = delete;

    // Set by host startup code after shared memory is created.
    void set_shared_memory(TubeSharedMemory* shm) {
        std::lock_guard<std::mutex> lock(mutex_);
        shared_memory_ = shm;
    }

    // Set the host UUID for inclusion in Connect responses.
    void set_host_uuid(const std::string& uuid) {
        std::lock_guard<std::mutex> lock(mutex_);
        host_uuid_ = uuid;
    }

    // --- RPC implementations ---

    grpc::Status Connect(
        grpc::ServerContext* context,
        const TubeConnectRequest* request,
        TubeConnectResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasTubeSocket<Memory>) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "Machine has no Tube socket");
        } else {
            auto& tube = machine_.state().memory.tube_socket;

            if (!tube.enabled()) {
                return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                    "Tube is not enabled");
            }

            if (!shared_memory_) {
                return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                    "Shared memory not initialised");
            }

            if (parasite_connected_) {
                return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                                    "A parasite is already connected");
            }

            // Record parasite details
            parasite_type_ = request->parasite_type();
            parasite_clock_hz_ = request->parasite_clock_hz();
            parasite_connected_ = true;
            connected_cv_.notify_all();

            // Return shared memory details
            response->set_shared_memory_name(shared_memory_->name());
            response->set_shared_memory_size(
                static_cast<uint32_t>(shared_memory_->size()));
            response->set_protocol_version(1);
            response->set_host_uuid(host_uuid_);

            return grpc::Status::OK;
        }
    }

    grpc::Status RegisterEndpoint(
        grpc::ServerContext* context,
        const RegisterEndpointRequest* request,
        RegisterEndpointResponse* response) override
    {
        (void)context;
        (void)response;
        std::lock_guard<std::mutex> lock(mutex_);

        if (!parasite_connected_) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "No parasite connected");
        }

        parasite_grpc_address_ = request->grpc_address();
        return grpc::Status::OK;
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

            if (!tube.enabled()) {
                return grpc::Status::OK;
            }

            response->set_parasite_connected(parasite_connected_);

            if (parasite_connected_) {
                response->set_parasite_type(parasite_type_);
                response->set_parasite_clock_hz(parasite_clock_hz_);
                if (!parasite_grpc_address_.empty()) {
                    response->set_parasite_grpc_address(parasite_grpc_address_);
                }
            }

            if (shared_memory_) {
                response->set_shared_memory_name(shared_memory_->name());
            }

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
        // State serialisation will be implemented when save/restore is needed.
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

    // Block until the parasite calls Connect, or until timeout_ms elapses.
    // Returns true if connected, false on timeout.
    bool wait_for_connection(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return connected_cv_.wait_for(lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return parasite_connected_; });
    }

    // Called when the parasite process disconnects or crashes.
    void notify_parasite_disconnected() {
        std::lock_guard<std::mutex> lock(mutex_);
        parasite_connected_ = false;
        parasite_type_.clear();
        parasite_clock_hz_ = 0;
        parasite_grpc_address_.clear();
    }

private:
    MachineType& machine_;
    std::mutex mutex_;
    std::condition_variable connected_cv_;

    // Non-owning pointer to the shared memory region (set by host startup).
    TubeSharedMemory* shared_memory_ = nullptr;

    // Parasite connection state.
    bool parasite_connected_ = false;
    std::string parasite_type_;
    uint32_t parasite_clock_hz_ = 0;
    std::string parasite_grpc_address_;
    std::string host_uuid_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_TUBE_SERVICE_HPP
