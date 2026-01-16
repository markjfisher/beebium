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

// Test gRPC SidewaysService
//
// These tests verify the sideways ROM/RAM service implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify sideways memory functionality.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "sideways.grpc.pb.h"
#include <grpcpp/grpcpp.h>

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

// Test fixture for Model B (aliased sideways with 4 sockets)
class ModelBSidewaysFixture {
public:
    ModelBSidewaysFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start();

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~ModelBSidewaysFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

// Test fixture for ROM/RAM board (16 independent slots)
class RomRamBoardSidewaysFixture {
public:
    RomRamBoardSidewaysFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelBRomRamBoard>>(machine_, "127.0.0.1", 0);
        server_->start();

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~RomRamBoardSidewaysFixture() {
        server_->stop();
    }

    beebium::ModelBRomRamBoard& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelBRomRamBoard machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBRomRamBoard>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// Model B (Aliased Sideways) Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SidewaysService GetSlotStatus returns aliasing info for Model B", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_aliasing() == true);
    CHECK(response.num_physical_slots() == 4);
    CHECK(response.sockets_size() == 4);
}

TEST_CASE("SidewaysService GetSlotStatus reports correct aliased slots for Model B", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 4);

    // Socket 0 (IC52) should alias slots 0, 4, 8, 12
    const auto& socket0 = response.sockets(0);
    CHECK(socket0.socket_index() == 0);
    REQUIRE(socket0.aliased_slots_size() == 4);
    CHECK(socket0.aliased_slots(0) == 0);
    CHECK(socket0.aliased_slots(1) == 4);
    CHECK(socket0.aliased_slots(2) == 8);
    CHECK(socket0.aliased_slots(3) == 12);

    // Socket 3 (IC101) should alias slots 3, 7, 11, 15 (BASIC)
    const auto& socket3 = response.sockets(3);
    CHECK(socket3.socket_index() == 3);
    REQUIRE(socket3.aliased_slots_size() == 4);
    CHECK(socket3.aliased_slots(0) == 3);
    CHECK(socket3.aliased_slots(1) == 7);
    CHECK(socket3.aliased_slots(2) == 11);
    CHECK(socket3.aliased_slots(3) == 15);
}

TEST_CASE("SidewaysService GetSlotStatus reports BASIC socket is populated", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 4);

    // Socket 3 (IC101) holds BASIC and should be populated
    const auto& socket3 = response.sockets(3);
    CHECK(socket3.populated() == true);
    CHECK(socket3.type() == beebium::SIDEWAYS_SLOT_TYPE_ROM);
}

TEST_CASE("SidewaysService ConfigureSlot with invalid slot returns error", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    request.set_slot(16);  // Invalid slot number
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == false);
    CHECK(response.error().find("Invalid slot") != std::string::npos);
}

TEST_CASE("SidewaysService ConfigureSlot maps slot to socket for Model B", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    // Configuring slot 4 should affect socket 0 (IC52) due to aliasing
    request.set_slot(4);
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    CHECK(response.actual_socket() == 0);  // slot 4 & 0x03 = 0
}

TEST_CASE("SidewaysService ReadSlotData reads ROM contents", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ReadSlotDataRequest request;
    beebium::ReadSlotDataResponse response;

    // Read first 16 bytes from BASIC ROM at slot 15 (socket 3)
    request.set_slot(15);
    request.set_offset(0);
    request.set_length(16);

    auto status = fixture.sideways().ReadSlotData(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    CHECK(response.data().size() == 16);
    CHECK(response.type() == beebium::SIDEWAYS_SLOT_TYPE_ROM);
}

TEST_CASE("SidewaysService ReadSlotData returns same content for aliased slots", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    // Read from slot 15 (BASIC)
    grpc::ClientContext context1;
    beebium::ReadSlotDataRequest request1;
    beebium::ReadSlotDataResponse response1;
    request1.set_slot(15);
    request1.set_offset(0);
    request1.set_length(16);
    auto status1 = fixture.sideways().ReadSlotData(&context1, request1, &response1);
    REQUIRE(status1.ok());

    // Read from slot 3 (also maps to IC101, same as slot 15)
    grpc::ClientContext context2;
    beebium::ReadSlotDataRequest request2;
    beebium::ReadSlotDataResponse response2;
    request2.set_slot(3);
    request2.set_offset(0);
    request2.set_length(16);
    auto status2 = fixture.sideways().ReadSlotData(&context2, request2, &response2);
    REQUIRE(status2.ok());

    // Both should return identical data due to aliasing
    CHECK(response1.data() == response2.data());
}

TEST_CASE("SidewaysService ReadSlotData with invalid slot returns error", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ReadSlotDataRequest request;
    beebium::ReadSlotDataResponse response;

    request.set_slot(16);  // Invalid
    request.set_offset(0);
    request.set_length(16);

    auto status = fixture.sideways().ReadSlotData(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == false);
    CHECK(response.error().find("Invalid slot") != std::string::npos);
}

TEST_CASE("SidewaysService ReadSlotData clamps length to slot boundary", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ReadSlotDataRequest request;
    beebium::ReadSlotDataResponse response;

    // Request extends past 16KB boundary
    request.set_slot(15);
    request.set_offset(16380);
    request.set_length(100);

    auto status = fixture.sideways().ReadSlotData(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    // Should be clamped to 4 bytes (16384 - 16380)
    CHECK(response.data().size() == 4);
}

//////////////////////////////////////////////////////////////////////////////
// ROM/RAM Board (Non-Aliased Sideways) Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SidewaysService GetSlotStatus returns 16 slots for ROM/RAM board", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_aliasing() == false);
    CHECK(response.num_physical_slots() == 16);
    CHECK(response.sockets_size() == 16);
}

TEST_CASE("SidewaysService GetSlotStatus reports no aliasing for ROM/RAM board", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());

    // Each slot should map only to itself
    for (int i = 0; i < 16; ++i) {
        const auto& slot = response.sockets(i);
        CHECK(slot.socket_index() == static_cast<uint32_t>(i));
        REQUIRE(slot.aliased_slots_size() == 1);
        CHECK(slot.aliased_slots(0) == static_cast<uint32_t>(i));
    }
}

TEST_CASE("SidewaysService ConfigureSlot actual_socket equals slot for ROM/RAM board", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    request.set_slot(7);
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    CHECK(response.actual_socket() == 7);  // No aliasing, socket = slot
}

TEST_CASE("SidewaysService ConfigureSlot with data loads image", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    // Configure slot 5 as ROM with test data
    grpc::ClientContext configure_context;
    beebium::ConfigureSlotRequest configure_request;
    beebium::ConfigureSlotResponse configure_response;

    configure_request.set_slot(5);
    configure_request.set_type(beebium::SIDEWAYS_SLOT_TYPE_ROM);

    // Create test data pattern
    std::string test_data(256, '\0');
    for (int i = 0; i < 256; ++i) {
        test_data[i] = static_cast<char>(i);
    }
    configure_request.set_data(test_data);

    auto configure_status = fixture.sideways().ConfigureSlot(
        &configure_context, configure_request, &configure_response);

    REQUIRE(configure_status.ok());
    REQUIRE(configure_response.success() == true);

    // Read back the data
    grpc::ClientContext read_context;
    beebium::ReadSlotDataRequest read_request;
    beebium::ReadSlotDataResponse read_response;

    read_request.set_slot(5);
    read_request.set_offset(0);
    read_request.set_length(256);

    auto read_status = fixture.sideways().ReadSlotData(
        &read_context, read_request, &read_response);

    REQUIRE(read_status.ok());
    REQUIRE(read_response.success() == true);
    CHECK(read_response.data() == test_data);
}

TEST_CASE("SidewaysService empty slot returns 0xFF", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    // Configure slot 10 as empty
    grpc::ClientContext configure_context;
    beebium::ConfigureSlotRequest configure_request;
    beebium::ConfigureSlotResponse configure_response;

    configure_request.set_slot(10);
    configure_request.set_type(beebium::SIDEWAYS_SLOT_TYPE_EMPTY);

    auto configure_status = fixture.sideways().ConfigureSlot(
        &configure_context, configure_request, &configure_response);

    REQUIRE(configure_status.ok());
    REQUIRE(configure_response.success() == true);

    // Read from empty slot should return 0xFF
    grpc::ClientContext read_context;
    beebium::ReadSlotDataRequest read_request;
    beebium::ReadSlotDataResponse read_response;

    read_request.set_slot(10);
    read_request.set_offset(0);
    read_request.set_length(16);

    auto read_status = fixture.sideways().ReadSlotData(
        &read_context, read_request, &read_response);

    REQUIRE(read_status.ok());
    REQUIRE(read_response.success() == true);
    CHECK(read_response.data().size() == 16);
    CHECK(read_response.type() == beebium::SIDEWAYS_SLOT_TYPE_EMPTY);

    // All bytes should be 0xFF
    for (char c : read_response.data()) {
        CHECK(static_cast<uint8_t>(c) == 0xFF);
    }
}
