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

// End-to-end gRPC test for EconetTransportService discovery RPCs.
//
// Two scenarios:
//   - Empty registry (no transport configured) -- ListTransports returns
//     no entries; GetActiveTransport returns an unset 'active' field.
//   - AUN extension in the registry -- both RPCs report it as active.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "beebium/Machines.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/service/EconetTransportService.hpp"
#include "beebium/service/Server.hpp"

#include "econet_transport.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>

namespace {

// Spin up a Server with only EconetTransportService bound to a
// caller-provided EconetTransportRegistry. No machine setup needed
// for these RPCs -- the service reads only from the registry.
class TransportDiscoveryFixture {
public:
    explicit TransportDiscoveryFixture(beebium::EconetTransportRegistry& registry)
        : service_(registry) {
        services_.push_back(&service_);

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
        stub_ = beebium::EconetTransportService::NewStub(channel_);
    }

    ~TransportDiscoveryFixture() {
        server_->stop();
    }

    beebium::EconetTransportService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::service::EconetTransportServiceImpl service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::EconetTransportService::Stub> stub_;
};

}  // namespace

TEST_CASE("EconetTransportService: empty registry reports no active transport",
          "[grpc][transport-discovery]") {
    beebium::EconetTransportRegistry registry;
    TransportDiscoveryFixture fixture(registry);

    {
        grpc::ClientContext ctx;
        beebium::ListTransportsRequest req;
        beebium::ListTransportsResponse resp;
        REQUIRE(fixture.stub().ListTransports(&ctx, req, &resp).ok());
        REQUIRE(resp.transports_size() == 0);
    }
    {
        grpc::ClientContext ctx;
        beebium::GetActiveTransportRequest req;
        beebium::GetActiveTransportResponse resp;
        REQUIRE(fixture.stub().GetActiveTransport(&ctx, req, &resp).ok());
        REQUIRE_FALSE(resp.has_active());
    }
}

TEST_CASE("EconetTransportService: AUN extension shows up as active",
          "[grpc][transport-discovery]") {
    beebium::EconetTransportRegistry registry;

    // Manifest matches what BuiltinExtensions::entries() supplies.
    auto aun = std::make_unique<beebium::AunEconetTransportExtension>();
    beebium::ExtensionManifest manifest;
    manifest.name = "aun";
    manifest.description = "AUN (Acorn Universal Networking) UDP econet transport";
    manifest.cli_name = "aun";
    manifest.extension_kind = "econet-transport";
    aun->set_manifest(std::move(manifest));
    registry.add(std::move(aun));

    TransportDiscoveryFixture fixture(registry);

    {
        grpc::ClientContext ctx;
        beebium::ListTransportsRequest req;
        beebium::ListTransportsResponse resp;
        REQUIRE(fixture.stub().ListTransports(&ctx, req, &resp).ok());
        REQUIRE(resp.transports_size() == 1);
        const auto& t = resp.transports(0);
        REQUIRE(t.name() == "aun");
        REQUIRE_FALSE(t.description().empty());
        REQUIRE(t.active());
    }
    {
        grpc::ClientContext ctx;
        beebium::GetActiveTransportRequest req;
        beebium::GetActiveTransportResponse resp;
        REQUIRE(fixture.stub().GetActiveTransport(&ctx, req, &resp).ok());
        REQUIRE(resp.has_active());
        REQUIRE(resp.active().name() == "aun");
        REQUIRE(resp.active().active());
    }
}
