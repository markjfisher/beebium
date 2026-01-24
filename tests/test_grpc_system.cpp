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

// Test gRPC SystemService
//
// These tests verify the SystemService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify system info and provenance.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/service/SystemService.hpp"

#include "system.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <fstream>
#include <vector>

namespace {

// Helper to load ROM file
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

// Test fixture that sets up a machine and server with system service
class SystemTestFixture {
public:
    explicit SystemTestFixture(beebium::service::Provenance provenance = {})
        : provenance_(std::move(provenance)) {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start(provenance_);

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        system_stub_ = beebium::SystemService::NewStub(channel_);
    }

    ~SystemTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::SystemService::Stub& system() { return *system_stub_; }

private:
    beebium::service::Provenance provenance_;
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SystemService::Stub> system_stub_;
};

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// GetSystemInfo Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SystemService GetSystemInfo returns machine type", "[grpc][system]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.machine_type() == "ModelB");
    CHECK_FALSE(response.machine_display_name().empty());
}

TEST_CASE("SystemService GetSystemInfo returns machine display name", "[grpc][system]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    // ModelB display name should mention "BBC" and "Model B"
    CHECK(response.machine_display_name().find("BBC") != std::string::npos);
    CHECK(response.machine_display_name().find("Model B") != std::string::npos);
}

//////////////////////////////////////////////////////////////////////////////
// Provenance Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SystemService GetSystemInfo returns provenance when provided", "[grpc][system][provenance]") {
    beebium::service::Provenance prov{
        "python-client",
        "550e8400-e29b-41d4-a716-446655440000",
        "1.2.3",
        std::chrono::system_clock::from_time_t(1700000000)
    };
    SystemTestFixture fixture(prov);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.has_provenance());

    const auto& returned_prov = response.provenance();
    CHECK(returned_prov.type() == "python-client");
    CHECK(returned_prov.instance_uuid() == "550e8400-e29b-41d4-a716-446655440000");
    CHECK(returned_prov.version() == "1.2.3");
    CHECK(returned_prov.timestamp() == 1700000000);
}

TEST_CASE("SystemService GetSystemInfo returns empty provenance when not provided", "[grpc][system][provenance]") {
    // Default empty provenance
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    // Provenance fields should be empty/default
    const auto& returned_prov = response.provenance();
    CHECK(returned_prov.type().empty());
    CHECK(returned_prov.instance_uuid().empty());
    CHECK(returned_prov.version().empty());
    CHECK(returned_prov.timestamp() == 0);
}

TEST_CASE("SystemService GetSystemInfo provenance type variations", "[grpc][system][provenance]") {
    SECTION("terminal type") {
        beebium::service::Provenance prov{"terminal", "uuid-1", "", std::chrono::system_clock::now()};
        SystemTestFixture fixture(prov);

        grpc::ClientContext context;
        beebium::GetSystemInfoRequest request;
        beebium::SystemInfo response;
        fixture.system().GetSystemInfo(&context, request, &response);

        CHECK(response.provenance().type() == "terminal");
    }

    SECTION("macos-gui type") {
        beebium::service::Provenance prov{"macos-gui", "uuid-2", "2.0.0", std::chrono::system_clock::now()};
        SystemTestFixture fixture(prov);

        grpc::ClientContext context;
        beebium::GetSystemInfoRequest request;
        beebium::SystemInfo response;
        fixture.system().GetSystemInfo(&context, request, &response);

        CHECK(response.provenance().type() == "macos-gui");
    }

    SECTION("typescript-oracle type") {
        beebium::service::Provenance prov{"typescript-oracle", "uuid-3", "0.1.0", std::chrono::system_clock::now()};
        SystemTestFixture fixture(prov);

        grpc::ClientContext context;
        beebium::GetSystemInfoRequest request;
        beebium::SystemInfo response;
        fixture.system().GetSystemInfo(&context, request, &response);

        CHECK(response.provenance().type() == "typescript-oracle");
    }
}

TEST_CASE("SystemService GetSystemInfo provenance timestamp conversion", "[grpc][system][provenance]") {
    // Test that chrono time_point is correctly converted to Unix timestamp
    auto now = std::chrono::system_clock::now();
    auto expected_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    beebium::service::Provenance prov{"test", "uuid", "1.0", now};
    SystemTestFixture fixture(prov);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    // Allow 1 second tolerance for test execution time
    CHECK(response.provenance().timestamp() >= expected_seconds - 1);
    CHECK(response.provenance().timestamp() <= expected_seconds + 1);
}

TEST_CASE("SystemService GetSystemInfo provenance with empty version", "[grpc][system][provenance]") {
    beebium::service::Provenance prov{
        "unknown",
        "12345678-1234-1234-1234-123456789abc",
        "",  // Empty version is valid
        std::chrono::system_clock::now()
    };
    SystemTestFixture fixture(prov);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.provenance().version().empty());
}

// Note: WatchServerStatus is a streaming RPC that requires special handling
// for testing. The server-side implementation sends READY immediately and then
// blocks waiting for shutdown or client disconnect. Testing this properly
// requires either async clients or multi-threaded test setup.
// For now, this functionality is covered by Python integration tests.

