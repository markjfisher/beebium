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

// End-to-end gRPC test for PiconetService.
//
// PiconetService is intentionally minimal at this stage -- one RPC,
// GetStatus, returning device_path and serial_open. The fixture wires
// a PiconetEconetTransportExtension to a FakePiconetDevice on a PTY
// pair, so the SerialPort actually opens (serial_open=true) and the
// device path the service reports is the slave end of the pair.

#include <catch2/catch_test_macros.hpp>

#include "PiconetEconetTransportExtension.hpp"
#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "piconet/FakePiconetDeviceOnPty.hpp"

#include "piconet_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>

namespace {

class PiconetServiceFixture {
public:
    PiconetServiceFixture() {
        machine_.reset();

        // FakePiconetDeviceOnPty owns its own PtyPair. The slave path
        // looks like a /dev/pts/N to PiconetBackend; opening it
        // succeeds, so the extension's serial_open transitions to true.
        fake_ = std::make_unique<beebium::piconet::test::FakePiconetDeviceOnPty>();
        REQUIRE(fake_->is_open());

        ext_ = std::make_unique<beebium::PiconetEconetTransportExtension>();
        ext_->set_config({{"device_path", fake_->slave_path()}});
        auto backend = ext_->create_backend(/*station=*/1);
        REQUIRE(backend != nullptr);
        machine_.state().memory.econet_socket.enable(
            /*station=*/1, std::move(backend), /*aun_mode=*/true);

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
        stub_ = beebium::PiconetService::NewStub(channel_);
    }

    ~PiconetServiceFixture() {
        server_->stop();
    }

    beebium::PiconetService::Stub& stub() { return *stub_; }
    const std::string& slave_path() const { return fake_->slave_path(); }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::piconet::test::FakePiconetDeviceOnPty> fake_;
    std::unique_ptr<beebium::PiconetEconetTransportExtension> ext_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::PiconetService::Stub> stub_;
};

}  // namespace

TEST_CASE("PiconetService GetStatus reports device path and open serial",
          "[grpc][piconet]") {
    PiconetServiceFixture fixture;

    grpc::ClientContext ctx;
    beebium::PiconetGetStatusRequest req;
    beebium::PiconetGetStatusResponse resp;
    auto status = fixture.stub().GetStatus(&ctx, req, &resp);

    REQUIRE(status.ok());
    REQUIRE(resp.device_path() == fixture.slave_path());
    REQUIRE(resp.serial_open());
}
