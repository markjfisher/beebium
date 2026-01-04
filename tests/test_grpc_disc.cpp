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

// Test gRPC DiscService
//
// These tests verify the DiscService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify disc operations work correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "disc.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <filesystem>
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

// Test fixture for Model B+ (which has disc drives)
class DiscTestFixtureBPlus {
public:
    DiscTestFixtureBPlus() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_2_0.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif
        machine_.reset();

        // Start server on a unique port to avoid conflicts
        server_ = std::make_unique<beebium::service::Server<beebium::ModelBPlus>>(
            machine_, "127.0.0.1", 50054);
        server_->start();

        // Create client channel
        channel_ = grpc::CreateChannel("127.0.0.1:50054",
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::DiscService::NewStub(channel_);
    }

    ~DiscTestFixtureBPlus() {
        server_->stop();
    }

    beebium::ModelBPlus& machine() { return machine_; }
    beebium::DiscService::Stub& stub() { return *stub_; }

private:
    beebium::ModelBPlus machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBPlus>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::DiscService::Stub> stub_;
};

// Test fixture for Model B (which does NOT have disc drives)
class DiscTestFixtureModelB {
public:
    DiscTestFixtureModelB() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif
        machine_.reset();

        // Start server on a unique port to avoid conflicts
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 50055);
        server_->start();

        // Create client channel
        channel_ = grpc::CreateChannel("127.0.0.1:50055",
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::DiscService::NewStub(channel_);
    }

    ~DiscTestFixtureModelB() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::DiscService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::DiscService::Stub> stub_;
};

// Get path to test disc image
std::filesystem::path get_test_disc_path() {
#ifdef BEEBIUM_DISCS_DIR
    return std::filesystem::path(BEEBIUM_DISCS_DIR) / "01-basic-validation.ssd";
#else
    return std::filesystem::path();
#endif
}

} // anonymous namespace

TEST_CASE("DiscService GetDriveStatus returns controller info for Model B+", "[grpc][disc]") {
    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::GetDriveStatusRequest request;
    beebium::GetDriveStatusResponse response;

    auto status = fixture.stub().GetDriveStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_disc_controller());
    CHECK(response.controller_type() == "WD1770");
    REQUIRE(response.drives_size() == 2);

    // Both drives should be empty initially
    CHECK(response.drives(0).state() == beebium::DISC_DRIVE_STATE_EMPTY);
    CHECK(response.drives(1).state() == beebium::DISC_DRIVE_STATE_EMPTY);
}

TEST_CASE("DiscService GetDriveStatus reports no controller for Model B", "[grpc][disc]") {
    DiscTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::GetDriveStatusRequest request;
    beebium::GetDriveStatusResponse response;

    auto status = fixture.stub().GetDriveStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.has_disc_controller());
    CHECK(response.controller_type().empty());
    CHECK(response.drives_size() == 0);
}

TEST_CASE("DiscService InsertDisc loads disc from filepath", "[grpc][disc]") {
    auto disc_path = get_test_disc_path();
    if (!std::filesystem::exists(disc_path)) {
        SKIP("Test disc image not available");
    }

    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    request.set_drive(0);
    request.set_url(disc_path.string());
    beebium::InsertDiscResponse response;

    auto status = fixture.stub().InsertDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.error().empty());
    CHECK(response.has_disc());
    CHECK(response.disc().sectors_per_track() == 10);
    CHECK(response.disc().sector_size() == 256);
}

TEST_CASE("DiscService InsertDisc loads disc from file:// URL", "[grpc][disc]") {
    auto disc_path = get_test_disc_path();
    if (!std::filesystem::exists(disc_path)) {
        SKIP("Test disc image not available");
    }

    DiscTestFixtureBPlus fixture;

    // Use file:// URL format
    std::string url = "file://" + disc_path.string();

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    request.set_drive(1);
    request.set_url(url);
    beebium::InsertDiscResponse response;

    auto status = fixture.stub().InsertDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.has_disc());
}

TEST_CASE("DiscService InsertDisc rejects invalid drive number", "[grpc][disc]") {
    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    request.set_drive(2);  // Invalid: only 0 and 1 are valid
    request.set_url("/some/path.ssd");
    beebium::InsertDiscResponse response;

    auto status = fixture.stub().InsertDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.success());
    CHECK(response.error().find("Invalid drive number") != std::string::npos);
}

TEST_CASE("DiscService InsertDisc fails for non-existent file", "[grpc][disc]") {
    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    request.set_drive(0);
    request.set_url("/non/existent/file.ssd");
    beebium::InsertDiscResponse response;

    auto status = fixture.stub().InsertDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.success());
    CHECK_FALSE(response.error().empty());
}

TEST_CASE("DiscService InsertDisc fails on Model B", "[grpc][disc]") {
    DiscTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    request.set_drive(0);
    request.set_url("/some/path.ssd");
    beebium::InsertDiscResponse response;

    auto status = fixture.stub().InsertDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.success());
    CHECK(response.error().find("no disc controller") != std::string::npos);
}

TEST_CASE("DiscService EjectDisc immediate eject works", "[grpc][disc]") {
    auto disc_path = get_test_disc_path();
    if (!std::filesystem::exists(disc_path)) {
        SKIP("Test disc image not available");
    }

    DiscTestFixtureBPlus fixture;

    // First insert a disc
    {
        grpc::ClientContext context;
        beebium::InsertDiscRequest request;
        request.set_drive(0);
        request.set_url(disc_path.string());
        beebium::InsertDiscResponse response;
        fixture.stub().InsertDisc(&context, request, &response);
        REQUIRE(response.success());
    }

    // Verify disc is loaded
    {
        grpc::ClientContext context;
        beebium::GetDriveStatusRequest request;
        beebium::GetDriveStatusResponse response;
        fixture.stub().GetDriveStatus(&context, request, &response);
        REQUIRE(response.drives(0).state() == beebium::DISC_DRIVE_STATE_LOADED);
    }

    // Now eject it
    {
        grpc::ClientContext context;
        beebium::EjectDiscRequest request;
        request.set_drive(0);
        request.set_immediate(true);
        beebium::EjectDiscResponse response;

        auto status = fixture.stub().EjectDisc(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.accepted());
    }

    // Verify disc is ejected
    {
        grpc::ClientContext context;
        beebium::GetDriveStatusRequest request;
        beebium::GetDriveStatusResponse response;
        fixture.stub().GetDriveStatus(&context, request, &response);
        CHECK(response.drives(0).state() == beebium::DISC_DRIVE_STATE_EMPTY);
    }
}

TEST_CASE("DiscService EjectDisc on empty drive fails", "[grpc][disc]") {
    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::EjectDiscRequest request;
    request.set_drive(0);
    request.set_immediate(true);
    beebium::EjectDiscResponse response;

    auto status = fixture.stub().EjectDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.accepted());
    CHECK(response.error().find("empty") != std::string::npos);
}

TEST_CASE("DiscService EjectDisc rejects invalid drive number", "[grpc][disc]") {
    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::EjectDiscRequest request;
    request.set_drive(5);  // Invalid
    beebium::EjectDiscResponse response;

    auto status = fixture.stub().EjectDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.accepted());
    CHECK(response.error().find("Invalid drive number") != std::string::npos);
}

TEST_CASE("DiscService InsertDisc with write_protect_override", "[grpc][disc]") {
    auto disc_path = get_test_disc_path();
    if (!std::filesystem::exists(disc_path)) {
        SKIP("Test disc image not available");
    }

    DiscTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::InsertDiscRequest request;
    request.set_drive(0);
    request.set_url(disc_path.string());
    request.set_write_protect_override(true);
    beebium::InsertDiscResponse response;

    auto status = fixture.stub().InsertDisc(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.disc().write_protected());
}

TEST_CASE("DiscService GetDriveStatus shows inserted disc info", "[grpc][disc]") {
    auto disc_path = get_test_disc_path();
    if (!std::filesystem::exists(disc_path)) {
        SKIP("Test disc image not available");
    }

    DiscTestFixtureBPlus fixture;

    // Insert disc
    {
        grpc::ClientContext context;
        beebium::InsertDiscRequest request;
        request.set_drive(0);
        request.set_url(disc_path.string());
        beebium::InsertDiscResponse response;
        fixture.stub().InsertDisc(&context, request, &response);
        REQUIRE(response.success());
    }

    // Get status
    grpc::ClientContext context;
    beebium::GetDriveStatusRequest request;
    beebium::GetDriveStatusResponse response;

    auto status = fixture.stub().GetDriveStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.drives_size() == 2);

    auto& drive0 = response.drives(0);
    CHECK(drive0.state() == beebium::DISC_DRIVE_STATE_LOADED);
    CHECK(drive0.disc_url() == disc_path.string());
    CHECK(drive0.has_disc());
    CHECK(drive0.disc().sectors_per_track() == 10);

    // Drive 1 should still be empty
    CHECK(response.drives(1).state() == beebium::DISC_DRIVE_STATE_EMPTY);
}

TEST_CASE("DiscService InsertDisc replaces existing disc", "[grpc][disc]") {
    auto disc_path = get_test_disc_path();
    if (!std::filesystem::exists(disc_path)) {
        SKIP("Test disc image not available");
    }

    DiscTestFixtureBPlus fixture;

    // Insert disc first time
    {
        grpc::ClientContext context;
        beebium::InsertDiscRequest request;
        request.set_drive(0);
        request.set_url(disc_path.string());
        beebium::InsertDiscResponse response;
        fixture.stub().InsertDisc(&context, request, &response);
        REQUIRE(response.success());
    }

    // Insert disc again (should replace)
    {
        grpc::ClientContext context;
        beebium::InsertDiscRequest request;
        request.set_drive(0);
        request.set_url(disc_path.string());
        request.set_write_protect_override(true);  // Different options
        beebium::InsertDiscResponse response;

        auto status = fixture.stub().InsertDisc(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.success());
        CHECK(response.disc().write_protected());  // New disc has different options
    }
}
