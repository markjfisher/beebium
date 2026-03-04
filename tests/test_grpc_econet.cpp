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

// Test gRPC EconetService
//
// These tests verify the EconetService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify Econet operations.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "econet.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <fstream>
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

class EconetTestFixture {
public:
    EconetTestFixture() {
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
        stub_ = beebium::EconetService::NewStub(channel_);
    }

    ~EconetTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::EconetService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::EconetService::Stub> stub_;
};

}  // namespace

// ============================================================================
// GetEconetStatus
// ============================================================================

TEST_CASE("EconetService GetEconetStatus when disabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetEconetStatusRequest request;
    beebium::GetEconetStatusResponse response;

    auto status = fixture.stub().GetEconetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.has_econet_socket());
    REQUIRE_FALSE(response.enabled());
    REQUIRE(response.station_id() == 0);
}

TEST_CASE("EconetService GetEconetStatus ADLC registers after enable", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::GetEconetStatusRequest request;
    beebium::GetEconetStatusResponse response;

    auto status = fixture.stub().GetEconetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.enabled());
    REQUIRE(response.station_id() == 5);
    REQUIRE(response.has_adlc());
    // After enable, ADLC should have valid register state
    CHECK(response.adlc().tx_frame_field() == "idle");
    CHECK(response.adlc().rx_frame_field() == "idle");
}

TEST_CASE("EconetService GetEconetStatus handshake stage", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable with AUN (ephemeral port)
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(10);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::GetEconetStatusRequest request;
    beebium::GetEconetStatusResponse response;

    auto status = fixture.stub().GetEconetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.aun_mode());
    REQUIRE(response.has_handshake());
    CHECK(response.handshake().stage() == "idle");
}

// ============================================================================
// EnableEconet
// ============================================================================

TEST_CASE("EconetService EnableEconet no_network", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(42);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.success());

    // Verify via GetEconetStatus
    grpc::ClientContext ctx2;
    beebium::GetEconetStatusRequest req2;
    beebium::GetEconetStatusResponse resp2;
    fixture.stub().GetEconetStatus(&ctx2, req2, &resp2);

    REQUIRE(resp2.enabled());
    REQUIRE(resp2.station_id() == 42);
    REQUIRE_FALSE(resp2.connected());
}

TEST_CASE("EconetService EnableEconet with AUN port", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(10);
    // Use port 0 which will give us the default (32768)
    // But that might conflict, so let's request a specific high port
    // Actually, use no specific port — the default of 0 in the proto
    // means "use default (32768)" per the proto definition.
    // To avoid port conflicts in tests, we should set no_network or
    // accept that the bind might fail if 32768 is in use.
    // Better: use an unlikely port.
    request.set_aun_port(0);  // Will use AUN_DEFAULT_PORT (32768)

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    // Port binding may fail if 32768 is in use, skip further checks
    if (response.success()) {
        CHECK(response.actual_aun_port() > 0);

        // Verify connected
        grpc::ClientContext ctx2;
        beebium::GetEconetStatusRequest req2;
        beebium::GetEconetStatusResponse resp2;
        fixture.stub().GetEconetStatus(&ctx2, req2, &resp2);
        CHECK(resp2.connected());
        CHECK(resp2.aun_port() > 0);
    }
}

TEST_CASE("EconetService EnableEconet invalid station 0", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(0);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("between 1 and 254") != std::string::npos);
}

TEST_CASE("EconetService EnableEconet invalid station 255", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(255);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("between 1 and 254") != std::string::npos);
}

TEST_CASE("EconetService EnableEconet when already enabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Try again
    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(10);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("already enabled") != std::string::npos);
}

// ============================================================================
// DisableEconet
// ============================================================================

TEST_CASE("EconetService DisableEconet", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::DisableEconetRequest request;
    beebium::DisableEconetResponse response;

    auto status = fixture.stub().DisableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.success());

    // Verify disabled
    grpc::ClientContext ctx2;
    beebium::GetEconetStatusRequest req2;
    beebium::GetEconetStatusResponse resp2;
    fixture.stub().GetEconetStatus(&ctx2, req2, &resp2);
    REQUIRE_FALSE(resp2.enabled());
}

TEST_CASE("EconetService DisableEconet when already disabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::DisableEconetRequest request;
    beebium::DisableEconetResponse response;

    auto status = fixture.stub().DisableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("not enabled") != std::string::npos);
}

// ============================================================================
// AddPeer / RemovePeer / ListPeers
// ============================================================================

TEST_CASE("EconetService AddPeer and ListPeers", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable with no_network (we don't need actual UDP for peer table management)
    // Actually, no_network uses TestBackend, not AunBackend, so AddPeer would fail.
    // We need AUN mode with a real AunBackend for peer management.
    // Use an ephemeral port to avoid conflicts.
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        // Don't set no_network — need AunBackend for peers
        fixture.stub().EnableEconet(&ctx, req, &resp);
        if (!resp.success()) {
            // Port 32768 may be in use — skip test
            SKIP("Could not bind AUN port: " + resp.error());
        }
    }

    // Add a peer
    {
        grpc::ClientContext ctx;
        beebium::AddPeerRequest req;
        beebium::AddPeerResponse resp;
        req.set_net(0);
        req.set_stn(254);
        req.set_ip_address("127.0.0.1");
        req.set_port(32768);

        auto status = fixture.stub().AddPeer(&ctx, req, &resp);
        REQUIRE(status.ok());
        REQUIRE(resp.success());
    }

    // List peers
    {
        grpc::ClientContext ctx;
        beebium::ListPeersRequest req;
        beebium::ListPeersResponse resp;

        auto status = fixture.stub().ListPeers(&ctx, req, &resp);
        REQUIRE(status.ok());
        REQUIRE(resp.peers_size() == 1);
        CHECK(resp.peers(0).net() == 0);
        CHECK(resp.peers(0).stn() == 254);
        CHECK(resp.peers(0).ip_address() == "127.0.0.1");
        CHECK(resp.peers(0).port() == 32768);
    }
}

TEST_CASE("EconetService AddPeer invalid IP", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        if (!resp.success()) {
            SKIP("Could not bind AUN port: " + resp.error());
        }
    }

    grpc::ClientContext context;
    beebium::AddPeerRequest request;
    beebium::AddPeerResponse response;
    request.set_net(0);
    request.set_stn(254);
    request.set_ip_address("not-an-ip");
    request.set_port(32768);

    auto status = fixture.stub().AddPeer(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("Invalid IP") != std::string::npos);
}

TEST_CASE("EconetService AddPeer when disabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::AddPeerRequest request;
    beebium::AddPeerResponse response;
    request.set_net(0);
    request.set_stn(254);
    request.set_ip_address("127.0.0.1");
    request.set_port(32768);

    auto status = fixture.stub().AddPeer(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("not enabled") != std::string::npos);
}

TEST_CASE("EconetService RemovePeer", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        if (!resp.success()) {
            SKIP("Could not bind AUN port: " + resp.error());
        }
    }

    // Add a peer
    {
        grpc::ClientContext ctx;
        beebium::AddPeerRequest req;
        beebium::AddPeerResponse resp;
        req.set_net(0);
        req.set_stn(254);
        req.set_ip_address("127.0.0.1");
        req.set_port(32768);
        fixture.stub().AddPeer(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Remove the peer
    {
        grpc::ClientContext ctx;
        beebium::RemovePeerRequest req;
        beebium::RemovePeerResponse resp;
        req.set_net(0);
        req.set_stn(254);

        auto status = fixture.stub().RemovePeer(&ctx, req, &resp);
        REQUIRE(status.ok());
        REQUIRE(resp.success());
    }

    // Verify empty
    {
        grpc::ClientContext ctx;
        beebium::ListPeersRequest req;
        beebium::ListPeersResponse resp;
        fixture.stub().ListPeers(&ctx, req, &resp);
        REQUIRE(resp.peers_size() == 0);
    }
}

TEST_CASE("EconetService ListPeers when empty", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        if (!resp.success()) {
            SKIP("Could not bind AUN port: " + resp.error());
        }
    }

    grpc::ClientContext context;
    beebium::ListPeersRequest request;
    beebium::ListPeersResponse response;

    auto status = fixture.stub().ListPeers(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.peers_size() == 0);
}

TEST_CASE("EconetService ListPeers multiple peers", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        if (!resp.success()) {
            SKIP("Could not bind AUN port: " + resp.error());
        }
    }

    // Add two peers
    {
        grpc::ClientContext ctx;
        beebium::AddPeerRequest req;
        beebium::AddPeerResponse resp;
        req.set_net(0);
        req.set_stn(254);
        req.set_ip_address("127.0.0.1");
        req.set_port(32768);
        fixture.stub().AddPeer(&ctx, req, &resp);
        REQUIRE(resp.success());
    }
    {
        grpc::ClientContext ctx;
        beebium::AddPeerRequest req;
        beebium::AddPeerResponse resp;
        req.set_net(0);
        req.set_stn(100);
        req.set_ip_address("192.168.1.1");
        req.set_port(12345);
        fixture.stub().AddPeer(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::ListPeersRequest request;
    beebium::ListPeersResponse response;

    auto status = fixture.stub().ListPeers(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.peers_size() == 2);
}

// ============================================================================
// SubscribeEconetEvents
// ============================================================================

TEST_CASE("EconetService SubscribeEconetEvents returns UNIMPLEMENTED", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeEconetEventsRequest request;

    auto reader = fixture.stub().SubscribeEconetEvents(&context, request);
    beebium::EconetEvent event;
    // The stream should immediately end with UNIMPLEMENTED
    bool read_result = reader->Read(&event);
    REQUIRE_FALSE(read_result);

    auto status = reader->Finish();
    REQUIRE(status.error_code() == grpc::StatusCode::UNIMPLEMENTED);
}
