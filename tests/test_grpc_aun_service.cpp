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

// End-to-end gRPC test for AunService. Spins up a real gRPC server with
// the AUN extension's service registered, installs an AunBackend on an
// OS-chosen port, and drives the AUN-specific RPCs (peer table
// management, cable plug, status query) through a real client stub.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "aun.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>

namespace {

class AunServiceFixture {
public:
    AunServiceFixture() {
        machine_.reset();

        // Build the AUN extension on an OS-assigned ephemeral port and
        // hand its backend to EconetSocket so AunService has something
        // to talk to. Using port=0 avoids cross-test conflicts on a
        // hardcoded port number.
        ext_ = std::make_unique<beebium::AunEconetTransportExtension>();
        ext_->set_config({{"port", "0"}});
        auto backend = ext_->create_backend(/*station=*/1);
        REQUIRE(backend != nullptr);
        machine_.state().memory.econet_socket.enable(
            /*station=*/1, std::move(backend), /*aun_mode=*/true);

        // Pick up the AunService instance the extension owns.
        services_ = ext_->grpc_services();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start(beebium::service::Provenance{},
                       beebium::service::MachineIdentity{},
                       /*enable_advertisement=*/false,
                       /*policy_config=*/{},
                       /*shutdown_callback=*/nullptr,
                       std::span<grpc::Service*>(services_));

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::AunService::NewStub(channel_);
    }

    ~AunServiceFixture() {
        server_->stop();
    }

    beebium::AunService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::AunEconetTransportExtension> ext_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::AunService::Stub> stub_;
};

}  // namespace

TEST_CASE("AunService GetStatus on a freshly-bound AUN backend", "[grpc][aun]") {
    AunServiceFixture fixture;

    grpc::ClientContext context;
    beebium::AunGetStatusRequest request;
    beebium::AunGetStatusResponse response;
    auto status = fixture.stub().GetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.connected());
    REQUIRE(response.local_port() != 0);
    REQUIRE(response.peer_count() == 0);
}

TEST_CASE("AunService AddPeer then ListPeers", "[grpc][aun]") {
    AunServiceFixture fixture;

    {
        grpc::ClientContext context;
        beebium::AunAddPeerRequest request;
        request.set_net(0);
        request.set_stn(254);
        request.set_ip_address("127.0.0.1");
        request.set_port(40001);
        beebium::AunAddPeerResponse response;
        auto status = fixture.stub().AddPeer(&context, request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.success());
    }

    {
        grpc::ClientContext context;
        beebium::AunListPeersRequest request;
        beebium::AunListPeersResponse response;
        auto status = fixture.stub().ListPeers(&context, request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.peers_size() == 1);
        const auto& peer = response.peers(0);
        REQUIRE(peer.net() == 0);
        REQUIRE(peer.stn() == 254);
        REQUIRE(peer.ip_address() == "127.0.0.1");
        REQUIRE(peer.port() == 40001);
    }
}

TEST_CASE("AunService AddPeer with default port (0) substitutes AUN_DEFAULT_PORT",
          "[grpc][aun]") {
    AunServiceFixture fixture;

    grpc::ClientContext add_ctx;
    beebium::AunAddPeerRequest add_req;
    add_req.set_net(0);
    add_req.set_stn(254);
    add_req.set_ip_address("127.0.0.1");
    add_req.set_port(0);  // request default
    beebium::AunAddPeerResponse add_resp;
    REQUIRE(fixture.stub().AddPeer(&add_ctx, add_req, &add_resp).ok());
    REQUIRE(add_resp.success());

    grpc::ClientContext list_ctx;
    beebium::AunListPeersRequest list_req;
    beebium::AunListPeersResponse list_resp;
    REQUIRE(fixture.stub().ListPeers(&list_ctx, list_req, &list_resp).ok());
    REQUIRE(list_resp.peers_size() == 1);
    REQUIRE(list_resp.peers(0).port() == 32768);  // AUN_DEFAULT_PORT
}

TEST_CASE("AunService RemovePeer", "[grpc][aun]") {
    AunServiceFixture fixture;

    {
        beebium::AunAddPeerRequest req;
        req.set_net(0); req.set_stn(254);
        req.set_ip_address("127.0.0.1"); req.set_port(40001);
        beebium::AunAddPeerResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(fixture.stub().AddPeer(&ctx, req, &resp).ok());
    }

    {
        beebium::AunRemovePeerRequest req;
        req.set_net(0); req.set_stn(254);
        beebium::AunRemovePeerResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(fixture.stub().RemovePeer(&ctx, req, &resp).ok());
        REQUIRE(resp.success());
    }

    {
        beebium::AunListPeersRequest req;
        beebium::AunListPeersResponse resp;
        grpc::ClientContext ctx;
        REQUIRE(fixture.stub().ListPeers(&ctx, req, &resp).ok());
        REQUIRE(resp.peers_size() == 0);
    }
}

TEST_CASE("AunService SetConnected toggles backend state", "[grpc][aun]") {
    AunServiceFixture fixture;

    {
        grpc::ClientContext ctx;
        beebium::AunSetConnectedRequest req;
        req.set_connected(false);
        beebium::AunSetConnectedResponse resp;
        REQUIRE(fixture.stub().SetConnected(&ctx, req, &resp).ok());
        REQUIRE(resp.success());
    }
    {
        grpc::ClientContext ctx;
        beebium::AunGetStatusRequest req;
        beebium::AunGetStatusResponse resp;
        REQUIRE(fixture.stub().GetStatus(&ctx, req, &resp).ok());
        REQUIRE_FALSE(resp.connected());
    }
    {
        grpc::ClientContext ctx;
        beebium::AunSetConnectedRequest req;
        req.set_connected(true);
        beebium::AunSetConnectedResponse resp;
        REQUIRE(fixture.stub().SetConnected(&ctx, req, &resp).ok());
    }
    {
        grpc::ClientContext ctx;
        beebium::AunGetStatusRequest req;
        beebium::AunGetStatusResponse resp;
        REQUIRE(fixture.stub().GetStatus(&ctx, req, &resp).ok());
        REQUIRE(resp.connected());
    }
}

TEST_CASE("AunService AddPeer rejects invalid IP", "[grpc][aun]") {
    AunServiceFixture fixture;

    grpc::ClientContext ctx;
    beebium::AunAddPeerRequest req;
    req.set_net(0); req.set_stn(254);
    req.set_ip_address("not-an-ip"); req.set_port(40001);
    beebium::AunAddPeerResponse resp;
    REQUIRE(fixture.stub().AddPeer(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.success());
    REQUIRE(resp.error().find("invalid") != std::string::npos);
}
