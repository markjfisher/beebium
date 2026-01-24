// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP
#define BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP

#include "system.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace beebium::service {

/// Launch provenance information
/// Identifies who/what launched the emulator core and when
struct Provenance {
    std::string type;
    std::string instance_uuid;
    std::string version;
    std::chrono::system_clock::time_point timestamp;
};

/// Machine identity information
/// Stable UUID and mutable name for identification and labeling
struct MachineIdentity {
    std::string uuid;       // RFC 4122 v4 UUID, stable for machine lifetime
    std::string name;       // User-assignable label, mutable
    std::string model_type; // e.g., "ModelB" (immutable)
    std::string model_name; // e.g., "BBC Model B" (immutable)
};

/// gRPC service implementation for SystemService
/// Provides machine configuration and identity information
template<typename MachineType>
class SystemServiceImpl final : public SystemService::Service {
public:
    SystemServiceImpl(MachineType& machine, Provenance provenance, MachineIdentity identity);
    ~SystemServiceImpl() override = default;

    // Non-copyable
    SystemServiceImpl(const SystemServiceImpl&) = delete;
    SystemServiceImpl& operator=(const SystemServiceImpl&) = delete;

    grpc::Status GetSystemInfo(
        grpc::ServerContext* context,
        const GetSystemInfoRequest* request,
        SystemInfo* response) override;

    grpc::Status SetMachineName(
        grpc::ServerContext* context,
        const SetMachineNameRequest* request,
        SetMachineNameResponse* response) override;

    grpc::Status WatchServerStatus(
        grpc::ServerContext* context,
        const WatchServerStatusRequest* request,
        grpc::ServerWriter<ServerStatusEvent>* writer) override;

    /// Notify all watchers that shutdown is imminent.
    /// Called from signal handler or shutdown path.
    /// Thread-safe: can be called from any thread.
    void notify_shutdown(uint32_t grace_ms = 5000);

private:
    /// Notify watchers that identity has changed.
    void notify_identity_changed();

    /// Populate a protobuf MachineIdentity message from the current identity.
    /// Caller must hold identity_mutex_ (unless called from construction).
    void populate_identity_proto(beebium::MachineIdentity* proto) const;

    MachineType& machine_;
    Provenance provenance_;
    MachineIdentity identity_;

    // Synchronization for identity changes and shutdown notification
    mutable std::mutex watchers_mutex_;
    std::condition_variable watchers_cv_;
    std::atomic<bool> shutdown_signaled_{false};
    std::atomic<uint32_t> shutdown_grace_ms_{5000};
    std::atomic<bool> identity_changed_{false};
};

//////////////////////////////////////////////////////////////////////////////
// SystemServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
SystemServiceImpl<MachineType>::SystemServiceImpl(
    MachineType& machine, Provenance provenance, MachineIdentity identity)
    : machine_(machine)
    , provenance_(std::move(provenance))
    , identity_(std::move(identity)) {
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::populate_identity_proto(
    beebium::MachineIdentity* proto) const {
    proto->set_uuid(identity_.uuid);
    proto->set_name(identity_.name);
    proto->set_model_type(identity_.model_type);
    proto->set_model_name(identity_.model_name);
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::GetSystemInfo(
    grpc::ServerContext* /*context*/,
    const GetSystemInfoRequest* /*request*/,
    SystemInfo* response) {

    // Set provenance information
    auto* prov = response->mutable_provenance();
    prov->set_type(provenance_.type);
    prov->set_instance_uuid(provenance_.instance_uuid);
    prov->set_version(provenance_.version);
    // Convert time_point to Unix timestamp (seconds since epoch)
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        provenance_.timestamp.time_since_epoch()).count();
    prov->set_timestamp(seconds);

    // Set identity information (thread-safe access to mutable name)
    auto* id = response->mutable_identity();
    {
        std::lock_guard<std::mutex> lock(watchers_mutex_);
        populate_identity_proto(id);
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::SetMachineName(
    grpc::ServerContext* /*context*/,
    const SetMachineNameRequest* request,
    SetMachineNameResponse* response) {

    // Validate name is not empty
    if (request->name().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Name cannot be empty");
    }

    // Update the name
    {
        std::lock_guard<std::mutex> lock(watchers_mutex_);
        identity_.name = request->name();
        populate_identity_proto(response->mutable_identity());
    }

    // Notify watchers of the change
    notify_identity_changed();

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::WatchServerStatus(
    grpc::ServerContext* context,
    const WatchServerStatusRequest* /*request*/,
    grpc::ServerWriter<ServerStatusEvent>* writer) {

    // Send READY event immediately upon subscription
    ServerStatusEvent ready_event;
    ready_event.set_status(SERVER_STATUS_READY);
    ready_event.set_message("Server ready");
    if (!writer->Write(ready_event)) {
        // Client disconnected before we could send READY
        return grpc::Status::OK;
    }

    // Wait for events in a loop
    while (!context->IsCancelled()) {
        std::unique_lock<std::mutex> lock(watchers_mutex_);
        watchers_cv_.wait(lock, [this, context] {
            return shutdown_signaled_.load() ||
                   identity_changed_.load() ||
                   context->IsCancelled();
        });

        if (context->IsCancelled()) {
            break;
        }

        // Handle identity change event
        if (identity_changed_.exchange(false)) {
            ServerStatusEvent event;
            event.set_status(SERVER_STATUS_IDENTITY_CHANGED);
            event.set_message("Machine identity changed");
            populate_identity_proto(event.mutable_identity());
            if (!writer->Write(event)) {
                break;
            }
        }

        // Handle shutdown event (terminal - exit the loop after sending)
        if (shutdown_signaled_.load()) {
            ServerStatusEvent event;
            event.set_status(SERVER_STATUS_SHUTTING_DOWN);
            event.set_message("Server shutting down");
            event.set_shutdown_grace_ms(shutdown_grace_ms_.load());
            writer->Write(event);
            break;
        }
    }

    return grpc::Status::OK;
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::notify_shutdown(uint32_t grace_ms) {
    shutdown_grace_ms_.store(grace_ms);
    shutdown_signaled_.store(true);
    watchers_cv_.notify_all();
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::notify_identity_changed() {
    identity_changed_.store(true);
    watchers_cv_.notify_all();
}

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP
