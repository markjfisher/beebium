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

// Test gRPC services on the parasite (ParasiteServer with ParasiteRunner).
//
// Creates a ParasiteRunner with stack-allocated TubeShared (no shared memory),
// wraps it in a ParasiteServer, connects as a gRPC client, and verifies the
// debugger RPCs work. DeviceInspection should return UNIMPLEMENTED since the
// parasite has no BBC Micro devices.

#include <catch2/catch_test_macros.hpp>

#include "beebium/tube/ParasiteRunner.hpp"
#include "beebium/tube/TubeShared.hpp"
#include "beebium/service/ParasiteServer.hpp"

#include "debugger.grpc.pb.h"
#include "system.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <array>
#include <cstdint>

namespace {

// Minimal 2 KB ROM that just loops: JMP $F800
std::array<uint8_t, 2048> make_loop_rom() {
    std::array<uint8_t, 2048> rom{};
    // JMP $F800 at $F800
    rom[0x000] = 0x4C;  // JMP abs
    rom[0x001] = 0x00;
    rom[0x002] = 0xF8;
    // Reset vector at $FFFC -> $F800
    rom[0x7FC] = 0x00;
    rom[0x7FD] = 0xF8;
    // NMI and IRQ vectors -> $F800
    rom[0x7FA] = 0x00;
    rom[0x7FB] = 0xF8;
    rom[0x7FE] = 0x00;
    rom[0x7FF] = 0xF8;
    return rom;
}

class ParasiteGrpcFixture {
public:
    ParasiteGrpcFixture()
        : rom_(make_loop_rom())
        , runner_(&shared_, rom_)
    {
        runner_.reset();
        // Run through reset sequence so PC is valid
        runner_.run(7);
        runner_.pause();

        server_ = std::make_unique<beebium::service::ParasiteServer>(
            runner_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        debugger_stub_ = beebium::DebuggerControl::NewStub(channel_);
        device_inspection_stub_ = beebium::DeviceInspection::NewStub(channel_);
        system_stub_ = beebium::SystemService::NewStub(channel_);
    }

    ~ParasiteGrpcFixture() {
        server_->stop();
    }

    beebium::ParasiteRunner& runner() { return runner_; }
    beebium::DebuggerControl::Stub& debugger() { return *debugger_stub_; }
    beebium::DeviceInspection::Stub& device_inspection() { return *device_inspection_stub_; }
    beebium::SystemService::Stub& system() { return *system_stub_; }

private:
    beebium::TubeShared shared_{};
    std::array<uint8_t, 2048> rom_;
    beebium::ParasiteRunner runner_;
    std::unique_ptr<beebium::service::ParasiteServer> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::DebuggerControl::Stub> debugger_stub_;
    std::unique_ptr<beebium::DeviceInspection::Stub> device_inspection_stub_;
    std::unique_ptr<beebium::SystemService::Stub> system_stub_;
};

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// DebuggerControl on parasite
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Parasite DebuggerControl GetState returns execution state", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::Empty request;
    beebium::ExecutionState response;

    auto status = fixture.debugger().GetState(&ctx, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.is_running());  // We paused after reset
    CHECK(response.cycle_count() == 7);
}

TEST_CASE("Parasite DebuggerControl StepInstruction works", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::StepRequest request;
    request.set_count(1);
    beebium::StepResponse response;

    auto status = fixture.debugger().StepInstruction(&ctx, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.instructions_executed() == 1);
    CHECK(response.cycles_executed() > 0);
}

TEST_CASE("Parasite DebuggerControl Get6502State returns CPU registers", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::Get6502StateRequest request;
    beebium::Cpu6502State response;

    auto status = fixture.debugger().Get6502State(&ctx, request, &response);

    REQUIRE(status.ok());
    // PC should be near the reset vector destination ($F800)
    CHECK(response.pc() >= 0xF800);
    CHECK(response.pc() <= 0xF803);
}

TEST_CASE("Parasite Get6502State includes interrupt handler state", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::Get6502StateRequest request;
    beebium::Cpu6502State response;

    auto status = fixture.debugger().Get6502State(&ctx, request, &response);

    REQUIRE(status.ok());
    // At rest, not inside any interrupt handler
    CHECK_FALSE(response.in_nmi_handler());
    CHECK_FALSE(response.in_irq_handler());
    CHECK_FALSE(response.nmi_pending());
}

TEST_CASE("Parasite DebuggerControl ReadMemory reads parasite RAM", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    // Write a value to RAM
    fixture.runner().write(0x1000, 0x42);

    grpc::ClientContext ctx;
    beebium::ReadMemoryRequest request;
    request.set_address(0x1000);
    request.set_length(1);
    beebium::ReadMemoryResponse response;

    auto status = fixture.debugger().ReadMemory(&ctx, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 1);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0x42);
}

TEST_CASE("Parasite DebuggerControl GetMemoryRegions returns Tube65C02 regions", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetMemoryRegionsRequest request;
    beebium::GetMemoryRegionsResponse response;

    auto status = fixture.debugger().GetMemoryRegions(&ctx, request, &response);

    REQUIRE(status.ok());
    CHECK(response.machine_type() == "Tube65C02");
    CHECK(response.regions_size() == 3);  // ram, rom, tube_registers
}

//////////////////////////////////////////////////////////////////////////////
// DeviceInspection should return UNIMPLEMENTED on parasite
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Parasite DeviceInspection GetSystemViaState returns UNIMPLEMENTED", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetSystemViaStateRequest request;
    beebium::ViaState response;

    auto status = fixture.device_inspection().GetSystemViaState(&ctx, request, &response);

    CHECK(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}

TEST_CASE("Parasite DeviceInspection GetCrtcState returns UNIMPLEMENTED", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetCrtcStateRequest request;
    beebium::CrtcState response;

    auto status = fixture.device_inspection().GetCrtcState(&ctx, request, &response);

    CHECK(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
// SystemService on parasite
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Parasite SystemService GetSystemInfo returns parasite identity", "[grpc][parasite]") {
    ParasiteGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&ctx, request, &response);

    REQUIRE(status.ok());
}
