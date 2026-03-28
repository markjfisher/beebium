// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_SCRATCH_RAM_SERVICE_HPP
#define BEEBIUM_EXTENSION_SCRATCH_RAM_SERVICE_HPP

#include "scratch_ram.grpc.pb.h"

#include <grpcpp/grpcpp.h>

namespace beebium {

class TestScratchRam;

class ScratchRamServiceImpl final : public ScratchRamService::Service {
public:
    explicit ScratchRamServiceImpl(TestScratchRam& ram) : ram_(ram) {}

    grpc::Status Read(
            grpc::ServerContext*,
            const ScratchRamReadRequest* request,
            ScratchRamReadResponse* response) override {
        auto offset = request->offset();
        if (offset >= 8) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "offset must be 0-7");
        }
        response->set_value(ram_.peek(static_cast<uint16_t>(offset)));
        return grpc::Status::OK;
    }

    grpc::Status Write(
            grpc::ServerContext*,
            const ScratchRamWriteRequest* request,
            ScratchRamWriteResponse*) override {
        auto offset = request->offset();
        if (offset >= 8) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "offset must be 0-7");
        }
        auto value = request->value();
        if (value > 255) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "value must be 0-255");
        }
        ram_.poke(static_cast<uint16_t>(offset), static_cast<uint8_t>(value));
        return grpc::Status::OK;
    }

    grpc::Status ReadAll(
            grpc::ServerContext*,
            const ScratchRamReadAllRequest*,
            ScratchRamReadAllResponse* response) override {
        std::string data(8, '\0');
        for (int i = 0; i < 8; ++i) {
            data[i] = static_cast<char>(ram_.peek(static_cast<uint16_t>(i)));
        }
        response->set_data(std::move(data));
        return grpc::Status::OK;
    }

private:
    TestScratchRam& ram_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_SCRATCH_RAM_SERVICE_HPP
