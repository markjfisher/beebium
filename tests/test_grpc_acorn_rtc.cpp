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

// Test gRPC AcornRtcService provided by the AcornRtcExtension.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "AcornRtcExtension.hpp"

#include "acorn_rtc.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace {

class AcornRtcGrpcFixture {
public:
    AcornRtcGrpcFixture() {
        machine_.reset();

        // Set up extension registry with user-port extension point
        registry_.register_extension_point("user-port");

        // Create and configure the RTC extension with a known time
        auto rtc = std::make_unique<beebium::AcornRtcExtension>();
        rtc->set_config({
            {"id", "test-rtc"},
            {"time", "1985-06-15T14:30"}
        });
        registry_.register_extension(std::move(rtc));

        beebium::ExtensionContext ctx(
            nullptr,  // no 1MHz bus needed
            &machine_.state().memory.user_port());
        registry_.resolve_and_init(ctx);

        auto ext_services = registry_.collect_grpc_services();

        // Start server with extension services
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {}, false, {}, nullptr, ext_services);

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::AcornRtcService::NewStub(channel_);
    }

    ~AcornRtcGrpcFixture() {
        server_->stop();
        registry_.shutdown();
    }

    beebium::AcornRtcService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::ExtensionRegistry registry_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::AcornRtcService::Stub> stub_;
};

}  // namespace

TEST_CASE("AcornRtcService GetTime returns initialised values",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetRtcTimeRequest request;
    beebium::GetRtcTimeResponse response;

    auto status = fixture.stub().GetTime(&ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.year() == 1985);
    REQUIRE(response.month() == 6);
    REQUIRE(response.day() == 15);
    REQUIRE(response.hour() == 14);
    REQUIRE(response.minute() == 30);
    REQUIRE(response.iso8601() == "1985-06-15T14:30");
}

TEST_CASE("AcornRtcService SetTime updates registers",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    // Set a new time
    {
        grpc::ClientContext ctx;
        beebium::SetRtcTimeRequest request;
        beebium::SetRtcTimeResponse response;
        request.set_iso8601("1990-12-25T08:00");
        auto status = fixture.stub().SetTime(&ctx, request, &response);
        REQUIRE(status.ok());
    }

    // Verify it took effect
    {
        grpc::ClientContext ctx;
        beebium::GetRtcTimeRequest request;
        beebium::GetRtcTimeResponse response;
        auto status = fixture.stub().GetTime(&ctx, request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.year() == 1990);
        REQUIRE(response.month() == 12);
        REQUIRE(response.day() == 25);
        REQUIRE(response.hour() == 8);
        REQUIRE(response.minute() == 0);
    }
}

TEST_CASE("AcornRtcService SetTime rejects out-of-range year",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::SetRtcTimeRequest request;
    beebium::SetRtcTimeResponse response;
    request.set_iso8601("2026-04-02T10:00");

    auto status = fixture.stub().SetTime(&ctx, request, &response);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("AcornRtcService GetRegisters returns 8 values",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetRtcRegistersRequest request;
    beebium::GetRtcRegistersResponse response;

    auto status = fixture.stub().GetRegisters(&ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.registers_size() == 8);

    // Verify counter register values (BCD encoded)
    REQUIRE(response.registers(0) == 0x06);  // month = June
    REQUIRE(response.registers(2) == 0x15);  // day = 15
    REQUIRE(response.registers(4) == 0x14);  // hour = 14
    REQUIRE(response.registers(6) == 0x30);  // minute = 30
    // Year register: 1985 - 1981 = 4
    REQUIRE(response.registers(1) == 0x04);
}

TEST_CASE("AcornRtcService SetRegister applies wrapping",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    // Write BCD 0x71 to register 7 (minute alarm, wrapping test)
    {
        grpc::ClientContext ctx;
        beebium::SetRtcRegisterRequest request;
        beebium::SetRtcRegisterResponse response;
        request.set_register_index(7);
        request.set_bcd_value(0x71);
        auto status = fixture.stub().SetRegister(&ctx, request, &response);
        REQUIRE(status.ok());
    }

    // Read back and verify wrapping: 0x71 (113) wraps to 13 (0x0D)
    {
        grpc::ClientContext ctx;
        beebium::GetRtcRegistersRequest request;
        beebium::GetRtcRegistersResponse response;
        auto status = fixture.stub().GetRegisters(&ctx, request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.registers(7) == 13);  // raw byte 13 = 0x0D
    }
}
