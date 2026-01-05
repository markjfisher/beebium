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

namespace beebium::service {

/// gRPC service implementation for SystemService
/// Provides machine configuration and identity information
template<typename MachineType>
class SystemServiceImpl final : public SystemService::Service {
public:
    explicit SystemServiceImpl(MachineType& machine);
    ~SystemServiceImpl() override = default;

    // Non-copyable
    SystemServiceImpl(const SystemServiceImpl&) = delete;
    SystemServiceImpl& operator=(const SystemServiceImpl&) = delete;

    grpc::Status GetSystemInfo(
        grpc::ServerContext* context,
        const GetSystemInfoRequest* request,
        SystemInfo* response) override;

private:
    MachineType& machine_;
};

//////////////////////////////////////////////////////////////////////////////
// SystemServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
SystemServiceImpl<MachineType>::SystemServiceImpl(MachineType& machine)
    : machine_(machine) {
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

    return grpc::Status::OK;
}

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP
