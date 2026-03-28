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

// Test gRPC ScratchRamService provided by the TestScratchRam extension.
//
// These tests verify that extension-provided gRPC services are registered
// with the server and accessible to clients. They also verify round-trip
// consistency between gRPC access and 6502-side memory access.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "TestScratchRam.hpp"

#include "scratch_ram.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace {

class ScratchRamGrpcFixture {
public:
    ScratchRamGrpcFixture() {
        machine_.reset();

        // Set up extension registry
        registry_.register_extension_point("1mhz-bus");
        registry_.register_extension(beebium::TestScratchRam::create());

        beebium::ExtensionContext ctx(&machine_.state().memory.one_mhz_bus());
        registry_.resolve_and_init(ctx);

        auto ext_services = registry_.collect_grpc_services();

        // Start server with extension services
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {}, false, {}, nullptr, ext_services);

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::ScratchRamService::NewStub(channel_);
    }

    ~ScratchRamGrpcFixture() {
        server_->stop();
        registry_.shutdown();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::ScratchRamService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::ExtensionRegistry registry_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ScratchRamService::Stub> stub_;
};

}  // namespace

TEST_CASE("ScratchRamService Read returns zero initially", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::ScratchRamReadRequest request;
    beebium::ScratchRamReadResponse response;
    request.set_offset(0);

    auto status = fixture.stub().Read(&ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.value() == 0);
}

TEST_CASE("ScratchRamService Write then Read round-trip", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write via gRPC
    {
        grpc::ClientContext ctx;
        beebium::ScratchRamWriteRequest request;
        beebium::ScratchRamWriteResponse response;
        request.set_offset(3);
        request.set_value(0xAB);
        auto status = fixture.stub().Write(&ctx, request, &response);
        REQUIRE(status.ok());
    }

    // Read back via gRPC
    {
        grpc::ClientContext ctx;
        beebium::ScratchRamReadRequest request;
        beebium::ScratchRamReadResponse response;
        request.set_offset(3);
        auto status = fixture.stub().Read(&ctx, request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.value() == 0xAB);
    }
}

TEST_CASE("ScratchRamService ReadAll returns all 8 bytes", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write some values via gRPC
    for (uint32_t i = 0; i < 8; ++i) {
        grpc::ClientContext ctx;
        beebium::ScratchRamWriteRequest request;
        beebium::ScratchRamWriteResponse response;
        request.set_offset(i);
        request.set_value(0x10 + i);
        fixture.stub().Write(&ctx, request, &response);
    }

    // ReadAll
    grpc::ClientContext ctx;
    beebium::ScratchRamReadAllRequest request;
    beebium::ScratchRamReadAllResponse response;
    auto status = fixture.stub().ReadAll(&ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 8);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(static_cast<uint8_t>(response.data()[i]) == 0x10 + i);
    }
}

TEST_CASE("ScratchRamService gRPC write visible to 6502 memory read", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write via gRPC
    {
        grpc::ClientContext ctx;
        beebium::ScratchRamWriteRequest request;
        beebium::ScratchRamWriteResponse response;
        request.set_offset(5);
        request.set_value(0xCD);
        fixture.stub().Write(&ctx, request, &response);
    }

    // Read via 6502 memory map (address 0xFC50 + 5 = 0xFC55)
    REQUIRE(fixture.machine().state().memory.read(0xFC55) == 0xCD);
}

TEST_CASE("ScratchRamService 6502 write visible to gRPC read", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write via 6502 memory map
    fixture.machine().state().memory.write(0xFC52, 0xEF);

    // Read via gRPC
    grpc::ClientContext ctx;
    beebium::ScratchRamReadRequest request;
    beebium::ScratchRamReadResponse response;
    request.set_offset(2);
    auto status = fixture.stub().Read(&ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.value() == 0xEF);
}

TEST_CASE("ScratchRamService Read rejects invalid offset", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::ScratchRamReadRequest request;
    beebium::ScratchRamReadResponse response;
    request.set_offset(8);  // out of range

    auto status = fixture.stub().Read(&ctx, request, &response);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}
