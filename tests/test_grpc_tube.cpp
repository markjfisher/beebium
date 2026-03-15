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

// Test gRPC TubeService
//
// These tests verify the TubeService implementation by acting as a gRPC client.
// They create a local server with a ModelB machine, connect to it, and verify
// Tube operations including Connect handshake and status queries.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/tube/TubeSharedMemory.hpp"

#include "tube.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <beebium/PlatformUtils.hpp>

#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> load_rom(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open ROM: " + filepath);
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Generate a unique suffix for shared memory names to avoid test collisions.
std::string unique_shm_suffix() {
    static int counter = 0;
    return "grpc_test_" + std::to_string(beebium::platform::get_pid()) + "_" + std::to_string(counter++);
}

class TubeTestFixture {
public:
    TubeTestFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::TubeService::NewStub(channel_);
    }

    ~TubeTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::TubeService::Stub& stub() { return *stub_; }
    beebium::service::Server<beebium::ModelB>& server() { return *server_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::TubeService::Stub> stub_;
};

}  // namespace

// ============================================================================
// GetStatus
// ============================================================================

TEST_CASE("TubeService GetStatus when disabled", "[grpc][tube]") {
    TubeTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetTubeStatusRequest request;
    beebium::GetTubeStatusResponse response;

    auto status = fixture.stub().GetStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_tube_socket());
    CHECK_FALSE(response.enabled());
    CHECK_FALSE(response.parasite_connected());
}

TEST_CASE("TubeService GetStatus when enabled but no parasite", "[grpc][tube]") {
    TubeTestFixture fixture;

    // Enable the Tube socket in in-process mode
    fixture.machine().state().memory.tube_socket.enable();

    grpc::ClientContext context;
    beebium::GetTubeStatusRequest request;
    beebium::GetTubeStatusResponse response;

    auto status = fixture.stub().GetStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_tube_socket());
    CHECK(response.enabled());
    CHECK_FALSE(response.parasite_connected());
}

TEST_CASE("TubeService GetStatus with shared memory", "[grpc][tube]") {
    TubeTestFixture fixture;

    auto suffix = unique_shm_suffix();
    beebium::TubeSharedMemory shm(suffix, beebium::TubeSharedMemoryRole::Creator);

    fixture.machine().state().memory.tube_socket.enable(shm.get());
    fixture.server().tube_service()->set_shared_memory(&shm);

    grpc::ClientContext context;
    beebium::GetTubeStatusRequest request;
    beebium::GetTubeStatusResponse response;

    auto status = fixture.stub().GetStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.enabled());
    CHECK_FALSE(response.parasite_connected());
    CHECK(response.shared_memory_name() == shm.name());
}

// ============================================================================
// Connect
// ============================================================================

TEST_CASE("TubeService Connect when Tube disabled", "[grpc][tube]") {
    TubeTestFixture fixture;

    grpc::ClientContext context;
    beebium::TubeConnectRequest request;
    beebium::TubeConnectResponse response;
    request.set_protocol_version(1);
    request.set_parasite_type("65C02");
    request.set_parasite_clock_hz(3000000);

    auto status = fixture.stub().Connect(&context, request, &response);

    REQUIRE_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
}

TEST_CASE("TubeService Connect when shared memory not set", "[grpc][tube]") {
    TubeTestFixture fixture;

    // Enable Tube in in-process mode (no shared memory pointer set on service)
    fixture.machine().state().memory.tube_socket.enable();

    grpc::ClientContext context;
    beebium::TubeConnectRequest request;
    beebium::TubeConnectResponse response;
    request.set_protocol_version(1);
    request.set_parasite_type("65C02");
    request.set_parasite_clock_hz(3000000);

    auto status = fixture.stub().Connect(&context, request, &response);

    REQUIRE_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
}

TEST_CASE("TubeService Connect successful handshake", "[grpc][tube]") {
    TubeTestFixture fixture;

    auto suffix = unique_shm_suffix();
    beebium::TubeSharedMemory shm(suffix, beebium::TubeSharedMemoryRole::Creator);

    fixture.machine().state().memory.tube_socket.enable(shm.get());
    fixture.server().tube_service()->set_shared_memory(&shm);

    grpc::ClientContext context;
    beebium::TubeConnectRequest request;
    beebium::TubeConnectResponse response;
    request.set_protocol_version(1);
    request.set_parasite_type("65C02");
    request.set_parasite_clock_hz(3000000);

    auto status = fixture.stub().Connect(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.shared_memory_name() == shm.name());
    CHECK(response.shared_memory_size() == static_cast<uint32_t>(shm.size()));
    CHECK(response.protocol_version() == 1);
}

TEST_CASE("TubeService Connect updates status", "[grpc][tube]") {
    TubeTestFixture fixture;

    auto suffix = unique_shm_suffix();
    beebium::TubeSharedMemory shm(suffix, beebium::TubeSharedMemoryRole::Creator);

    fixture.machine().state().memory.tube_socket.enable(shm.get());
    fixture.server().tube_service()->set_shared_memory(&shm);

    // Connect
    {
        grpc::ClientContext ctx;
        beebium::TubeConnectRequest req;
        beebium::TubeConnectResponse resp;
        req.set_protocol_version(1);
        req.set_parasite_type("65C02");
        req.set_parasite_clock_hz(3000000);

        auto status = fixture.stub().Connect(&ctx, req, &resp);
        REQUIRE(status.ok());
    }

    // Check status reflects the connected parasite
    grpc::ClientContext context;
    beebium::GetTubeStatusRequest request;
    beebium::GetTubeStatusResponse response;

    auto status = fixture.stub().GetStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.parasite_connected());
    CHECK(response.parasite_type() == "65C02");
    CHECK(response.parasite_clock_hz() == 3000000);
}

TEST_CASE("TubeService Connect rejects second parasite", "[grpc][tube]") {
    TubeTestFixture fixture;

    auto suffix = unique_shm_suffix();
    beebium::TubeSharedMemory shm(suffix, beebium::TubeSharedMemoryRole::Creator);

    fixture.machine().state().memory.tube_socket.enable(shm.get());
    fixture.server().tube_service()->set_shared_memory(&shm);

    // First connect succeeds
    {
        grpc::ClientContext ctx;
        beebium::TubeConnectRequest req;
        beebium::TubeConnectResponse resp;
        req.set_protocol_version(1);
        req.set_parasite_type("65C02");
        req.set_parasite_clock_hz(3000000);

        auto status = fixture.stub().Connect(&ctx, req, &resp);
        REQUIRE(status.ok());
    }

    // Second connect fails
    {
        grpc::ClientContext ctx;
        beebium::TubeConnectRequest req;
        beebium::TubeConnectResponse resp;
        req.set_protocol_version(1);
        req.set_parasite_type("Z80");
        req.set_parasite_clock_hz(4000000);

        auto status = fixture.stub().Connect(&ctx, req, &resp);
        REQUIRE_FALSE(status.ok());
        CHECK(status.error_code() == grpc::StatusCode::ALREADY_EXISTS);
    }
}

TEST_CASE("TubeService Connect allowed after disconnect notification", "[grpc][tube]") {
    TubeTestFixture fixture;

    auto suffix = unique_shm_suffix();
    beebium::TubeSharedMemory shm(suffix, beebium::TubeSharedMemoryRole::Creator);

    fixture.machine().state().memory.tube_socket.enable(shm.get());
    fixture.server().tube_service()->set_shared_memory(&shm);

    // First connect
    {
        grpc::ClientContext ctx;
        beebium::TubeConnectRequest req;
        beebium::TubeConnectResponse resp;
        req.set_protocol_version(1);
        req.set_parasite_type("65C02");
        req.set_parasite_clock_hz(3000000);

        auto status = fixture.stub().Connect(&ctx, req, &resp);
        REQUIRE(status.ok());
    }

    // Notify disconnect
    fixture.server().tube_service()->notify_parasite_disconnected();

    // Second connect succeeds
    {
        grpc::ClientContext ctx;
        beebium::TubeConnectRequest req;
        beebium::TubeConnectResponse resp;
        req.set_protocol_version(1);
        req.set_parasite_type("Z80");
        req.set_parasite_clock_hz(4000000);

        auto status = fixture.stub().Connect(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.shared_memory_name() == shm.name());
    }

    // Status reflects new parasite
    {
        grpc::ClientContext ctx;
        beebium::GetTubeStatusRequest req;
        beebium::GetTubeStatusResponse resp;

        auto status = fixture.stub().GetStatus(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.parasite_type() == "Z80");
        CHECK(resp.parasite_clock_hz() == 4000000);
    }
}

// ============================================================================
// GetState / RestoreState (not yet implemented)
// ============================================================================

TEST_CASE("TubeService GetState returns UNIMPLEMENTED", "[grpc][tube]") {
    TubeTestFixture fixture;

    grpc::ClientContext context;
    beebium::TubeGetStateRequest request;
    beebium::TubeGetStateResponse response;

    auto status = fixture.stub().GetState(&context, request, &response);

    REQUIRE_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}

TEST_CASE("TubeService RestoreState returns UNIMPLEMENTED", "[grpc][tube]") {
    TubeTestFixture fixture;

    grpc::ClientContext context;
    beebium::TubeRestoreStateRequest request;
    beebium::TubeRestoreStateResponse response;

    auto status = fixture.stub().RestoreState(&context, request, &response);

    REQUIRE_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}
