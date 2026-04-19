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

#include "PiconetService.hpp"

#include "PiconetEconetTransportExtension.hpp"
#include "beebium/econet/PiconetBackend.hpp"

namespace beebium {

PiconetServiceImpl::PiconetServiceImpl(PiconetEconetTransportExtension& extension)
    : extension_(extension) {}

grpc::Status PiconetServiceImpl::GetStatus(
        grpc::ServerContext*,
        const PiconetGetStatusRequest*,
        PiconetGetStatusResponse* response) {
    auto* backend = extension_.backend();
    if (!backend) {
        // Extension is loaded but the backend never came up (device
        // path missing or open failed). Report empty path + closed.
        response->set_device_path(std::string{});
        response->set_serial_open(false);
        return grpc::Status::OK;
    }
    response->set_device_path(backend->config().device_path);
    response->set_serial_open(backend->is_connected());
    return grpc::Status::OK;
}

}  // namespace beebium
