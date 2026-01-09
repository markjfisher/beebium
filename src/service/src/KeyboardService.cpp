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

#include "beebium/service/KeyboardService.hpp"
#include "beebium/SystemViaPeripheral.hpp"

namespace beebium::service {

KeyboardServiceImpl::KeyboardServiceImpl(SystemViaPeripheral& keyboard)
    : keyboard_(keyboard) {
}

KeyboardServiceImpl::~KeyboardServiceImpl() = default;

grpc::Status KeyboardServiceImpl::KeyDown(
    grpc::ServerContext* /*context*/,
    const KeyRequest* request,
    KeyResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t ik_number = request->ik_number();
    uint32_t row = (ik_number >> 4) & 0x0F;
    uint32_t column = ik_number & 0x0F;

    if (row >= 10 || column >= 10) {
        response->set_accepted(false);
        return grpc::Status::OK;
    }

    keyboard_.key_down(static_cast<uint8_t>(row), static_cast<uint8_t>(column));
    response->set_accepted(true);

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::KeyUp(
    grpc::ServerContext* /*context*/,
    const KeyRequest* request,
    KeyResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t ik_number = request->ik_number();
    uint32_t row = (ik_number >> 4) & 0x0F;
    uint32_t column = ik_number & 0x0F;

    if (row >= 10 || column >= 10) {
        response->set_accepted(false);
        return grpc::Status::OK;
    }

    keyboard_.key_up(static_cast<uint8_t>(row), static_cast<uint8_t>(column));
    response->set_accepted(true);

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetState(
    grpc::ServerContext* /*context*/,
    const GetStateRequest* /*request*/,
    KeyboardState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Get keyboard matrix state from peripheral
    for (int row = 0; row < 10; ++row) {
        response->add_pressed_rows(keyboard_.get_row_state(row));
    }

    return grpc::Status::OK;
}

// =============================================================================
// Keyboard Links (raw byte access)
// =============================================================================

grpc::Status KeyboardServiceImpl::SetLinks(
    grpc::ServerContext* /*context*/,
    const SetLinksRequest* request,
    SetLinksResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t value = request->value();
    if (value > 255) {
        response->set_success(false);
        response->set_error("value must be 0-255");
        return grpc::Status::OK;
    }

    keyboard_.keyboard().set_startup_options(static_cast<uint8_t>(value));
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetLinks(
    grpc::ServerContext* /*context*/,
    const GetLinksRequest* /*request*/,
    LinksState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_value(keyboard_.keyboard().startup_options());
    return grpc::Status::OK;
}

// =============================================================================
// Semantic accessors
// =============================================================================

grpc::Status KeyboardServiceImpl::SetStartupScreenMode(
    grpc::ServerContext* /*context*/,
    const SetStartupScreenModeRequest* request,
    SetStartupScreenModeResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t mode = request->mode();
    if (mode > 7) {
        response->set_success(false);
        response->set_error("mode must be 0-7");
        return grpc::Status::OK;
    }

    keyboard_.keyboard().set_screen_mode(static_cast<uint8_t>(mode));
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetStartupScreenMode(
    grpc::ServerContext* /*context*/,
    const GetStartupScreenModeRequest* /*request*/,
    StartupScreenModeState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_mode(keyboard_.keyboard().screen_mode());
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::SetStartupAutoBoot(
    grpc::ServerContext* /*context*/,
    const SetStartupAutoBootRequest* request,
    SetStartupAutoBootResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    keyboard_.keyboard().set_auto_boot(request->enabled());
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetStartupAutoBoot(
    grpc::ServerContext* /*context*/,
    const GetStartupAutoBootRequest* /*request*/,
    StartupAutoBootState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_enabled(keyboard_.keyboard().auto_boot());
    return grpc::Status::OK;
}

} // namespace beebium::service
