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

    uint32_t row = request->row();
    uint32_t column = request->column();

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

    uint32_t row = request->row();
    uint32_t column = request->column();

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

grpc::Status KeyboardServiceImpl::SetStartupOptions(
    grpc::ServerContext* /*context*/,
    const SetStartupOptionsRequest* request,
    SetStartupOptionsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto& matrix = keyboard_.keyboard();

    switch (request->options_case()) {
        case SetStartupOptionsRequest::kRawByte: {
            uint32_t raw = request->raw_byte();
            if (raw > 255) {
                response->set_success(false);
                response->set_error("raw_byte must be 0-255");
                return grpc::Status::OK;
            }
            matrix.set_startup_options(static_cast<uint8_t>(raw));
            break;
        }

        case SetStartupOptionsRequest::kSemantic: {
            const auto& semantic = request->semantic();

            if (semantic.has_screen_mode()) {
                uint32_t mode = semantic.screen_mode();
                if (mode > 7) {
                    response->set_success(false);
                    response->set_error("screen_mode must be 0-7");
                    return grpc::Status::OK;
                }
                matrix.set_screen_mode(static_cast<uint8_t>(mode));
            }

            if (semantic.has_auto_boot()) {
                matrix.set_auto_boot(semantic.auto_boot());
            }
            break;
        }

        case SetStartupOptionsRequest::OPTIONS_NOT_SET:
            // No options provided - nothing to do
            break;
    }

    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetStartupOptions(
    grpc::ServerContext* /*context*/,
    const GetStartupOptionsRequest* /*request*/,
    StartupOptions* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto& matrix = keyboard_.keyboard();

    response->set_raw_byte(matrix.startup_options());
    response->set_screen_mode(matrix.screen_mode());
    response->set_auto_boot(matrix.auto_boot());

    return grpc::Status::OK;
}

} // namespace beebium::service
