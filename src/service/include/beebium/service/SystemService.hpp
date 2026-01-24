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

/// gRPC service implementation for SystemService
/// Provides machine configuration and identity information
template<typename MachineType>
class SystemServiceImpl final : public SystemService::Service {
public:
    SystemServiceImpl(MachineType& machine, Provenance provenance);
    ~SystemServiceImpl() override = default;

    // Non-copyable
    SystemServiceImpl(const SystemServiceImpl&) = delete;
    SystemServiceImpl& operator=(const SystemServiceImpl&) = delete;

    grpc::Status GetSystemInfo(
        grpc::ServerContext* context,
        const GetSystemInfoRequest* request,
        SystemInfo* response) override;

    grpc::Status WatchServerStatus(
        grpc::ServerContext* context,
        const WatchServerStatusRequest* request,
        grpc::ServerWriter<ServerStatusEvent>* writer) override;

    /// Notify all watchers that shutdown is imminent.
    /// Called from signal handler or shutdown path.
    /// Thread-safe: can be called from any thread.
    void notify_shutdown(uint32_t grace_ms = 5000);

private:
    MachineType& machine_;
    Provenance provenance_;

    // Shutdown notification state
    mutable std::mutex watchers_mutex_;
    std::condition_variable shutdown_cv_;
    std::atomic<bool> shutdown_signaled_{false};
    std::atomic<uint32_t> shutdown_grace_ms_{5000};
};

//////////////////////////////////////////////////////////////////////////////
// SystemServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
SystemServiceImpl<MachineType>::SystemServiceImpl(MachineType& machine, Provenance provenance)
    : machine_(machine), provenance_(std::move(provenance)) {
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::GetSystemInfo(
    grpc::ServerContext* /*context*/,
    const GetSystemInfoRequest* /*request*/,
    SystemInfo* response) {

    // Get machine type and display name from hardware
    using Memory = typename MachineType::Memory;
    response->set_machine_type(std::string(Memory::MACHINE_TYPE));
    response->set_machine_display_name(std::string(Memory::MACHINE_DISPLAY_NAME));

    // Set provenance information
    auto* prov = response->mutable_provenance();
    prov->set_type(provenance_.type);
    prov->set_instance_uuid(provenance_.instance_uuid);
    prov->set_version(provenance_.version);
    // Convert time_point to Unix timestamp (seconds since epoch)
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        provenance_.timestamp.time_since_epoch()).count();
    prov->set_timestamp(seconds);

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

    // Wait for either shutdown signal or client disconnect
    {
        std::unique_lock<std::mutex> lock(watchers_mutex_);
        shutdown_cv_.wait(lock, [this, context] {
            return shutdown_signaled_.load() || context->IsCancelled();
        });
    }

    // If shutdown was signaled (and client still connected), send shutdown event
    if (shutdown_signaled_.load() && !context->IsCancelled()) {
        ServerStatusEvent shutdown_event;
        shutdown_event.set_status(SERVER_STATUS_SHUTTING_DOWN);
        shutdown_event.set_message("Server shutting down");
        shutdown_event.set_shutdown_grace_ms(shutdown_grace_ms_.load());
        writer->Write(shutdown_event);
    }

    return grpc::Status::OK;
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::notify_shutdown(uint32_t grace_ms) {
    shutdown_grace_ms_.store(grace_ms);
    shutdown_signaled_.store(true);
    shutdown_cv_.notify_all();
}

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP
