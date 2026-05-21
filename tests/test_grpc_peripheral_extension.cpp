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

// Test gRPC PeripheralExtensionService for frontend discovery of loaded extensions.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionStorage.hpp"
#include "beebium/extension/ExtensionUi.hpp"
#include "beebium/extension/PeripheralExtension.hpp"
#include "beebium/service/PeripheralExtensionService.hpp"
#include "TestScratchRam.hpp"

#include "peripheral_extension.grpc.pb.h"
#include "extension_ui.pb.h"
#include <grpcpp/grpcpp.h>

namespace {

// A no-op ExtensionUi just to make ui() non-null. ListExtensions only
// cares whether ui() returns a pointer, not what the View looks like.
class NoopUi : public beebium::ExtensionUi {
public:
    void build_view(beebium::View*) const override {}
    void handle_event(const beebium::DispatchRequest&) override {}
};

// Minimal PeripheralExtension that exposes a UI. Used to assert the
// server populates ExtensionInfo.has_ui correctly.
class UiBearingTestExtension : public beebium::PeripheralExtension {
public:
    UiBearingTestExtension() {
        beebium::ExtensionManifest m;
        m.name = "ui-bearing";
        m.display_name = "UI-Bearing Test Extension";
        m.library_stem = "ui-bearing";
        set_manifest(std::move(m));
    }

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }
    std::span<const std::string_view> provides() const override { return {}; }

    void init(beebium::ExtensionContext&) override {}
    void shutdown() override {}

    beebium::ExtensionUi* ui() override { return &ui_; }

private:
    NoopUi ui_;
};

// PeripheralExtension that publishes a single storage device, plus a
// second one to exercise the multi-device-per-extension case. Used
// to assert PeripheralExtensionServiceImpl serialises StorageDevice
// entries through to the proto response.
class StorageBearingTestExtension : public beebium::PeripheralExtension,
                                    public beebium::ExtensionStorage {
public:
    StorageBearingTestExtension() {
        beebium::ExtensionManifest m;
        m.name = "storage-bearing";
        m.display_name = "Storage-Bearing Test Extension";
        m.library_stem = "storage-bearing";
        set_manifest(std::move(m));
    }

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }
    std::span<const std::string_view> provides() const override { return {}; }

    void init(beebium::ExtensionContext&) override {}
    void shutdown() override {}

    beebium::ExtensionStorage* storage() override { return this; }

    std::vector<beebium::StorageDeviceInfo> devices() const override {
        beebium::StorageDeviceInfo fixed;
        fixed.id = "test-fixed";
        fixed.name = "Test Fixed Device";
        fixed.kind = beebium::StorageDeviceInfo::Kind::Fixed;
        fixed.media_type = "test-fixed-media";
        fixed.backing_path = "/tmp/fixed.dat";
        fixed.activity_indicator_name = "test-fixed-led";

        beebium::StorageDeviceInfo removable;
        removable.id = "test-removable";
        removable.name = "Test Removable Device";
        removable.kind = beebium::StorageDeviceInfo::Kind::Removable;
        removable.media_type = "test-removable-media";
        removable.backing_path = "";  // no media inserted

        return {std::move(fixed), std::move(removable)};
    }
};

class PeripheralExtensionGrpcFixture {
public:
    PeripheralExtensionGrpcFixture() {
        machine_.reset();

        registry_.register_extension_point("1mhz-bus");
        registry_.register_extension(beebium::TestScratchRam::create());

        beebium::ExtensionContext ctx(&machine_.state().memory.one_mhz_bus());
        registry_.resolve_and_init(ctx);

        peripheral_ext_service_ =
            std::make_unique<beebium::service::PeripheralExtensionServiceImpl>(registry_);

        auto ext_services = registry_.collect_grpc_services();
        ext_services.push_back(peripheral_ext_service_.get());

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {}, false, {}, nullptr, ext_services);

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::PeripheralExtensionService::NewStub(channel_);
    }

    ~PeripheralExtensionGrpcFixture() {
        server_->stop();
        registry_.shutdown();
    }

    beebium::PeripheralExtensionService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::ExtensionRegistry registry_;
    std::unique_ptr<beebium::service::PeripheralExtensionServiceImpl> peripheral_ext_service_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::PeripheralExtensionService::Stub> stub_;
};

}  // namespace

TEST_CASE("PeripheralExtensionService ListExtensions returns loaded extensions",
          "[grpc][extension][discovery]") {
    PeripheralExtensionGrpcFixture fixture;

    grpc::ClientContext ctx;
    beebium::ListExtensionsRequest request;
    beebium::ListExtensionsResponse response;

    auto status = fixture.stub().ListExtensions(&ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.extensions_size() == 1);

    auto& ext = response.extensions(0);
    REQUIRE(ext.name() == "test-scratch-ram");
    REQUIRE(ext.attaches_to_size() == 1);
    REQUIRE(ext.attaches_to(0) == "1mhz-bus");
    REQUIRE(ext.provides_size() == 0);
    // TestScratchRam doesn't override Extension::ui(), so the server
    // must report has_ui=false. The macOS frontend uses this flag to
    // decide whether to subscribe to ExtensionUiService for the node.
    REQUIRE_FALSE(ext.has_ui());
}

TEST_CASE("PeripheralExtensionService ListExtensions reports has_ui correctly",
          "[grpc][extension][discovery]") {
    // Register both a UI-bearing and a UI-less extension and assert
    // the server populates has_ui per-instance.
    beebium::ModelB machine;
    machine.reset();

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    registry.register_extension(beebium::TestScratchRam::create());
    registry.register_extension(std::make_unique<UiBearingTestExtension>());

    beebium::ExtensionContext ctx(&machine.state().memory.one_mhz_bus());
    registry.resolve_and_init(ctx);

    beebium::service::PeripheralExtensionServiceImpl peripheral_ext_service(registry);
    auto ext_services = registry.collect_grpc_services();
    ext_services.push_back(&peripheral_ext_service);

    beebium::service::Server<beebium::ModelB> server(machine, "127.0.0.1", 0);
    server.start({}, {}, false, {}, nullptr, ext_services);

    auto channel = grpc::CreateChannel(
        "127.0.0.1:" + std::to_string(server.port()),
        grpc::InsecureChannelCredentials());
    auto stub = beebium::PeripheralExtensionService::NewStub(channel);

    grpc::ClientContext grpc_ctx;
    beebium::ListExtensionsRequest request;
    beebium::ListExtensionsResponse response;

    auto status = stub->ListExtensions(&grpc_ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.extensions_size() == 2);

    bool saw_test_scratch_ram = false;
    bool saw_ui_bearing = false;
    for (int i = 0; i < response.extensions_size(); ++i) {
        const auto& info = response.extensions(i);
        if (info.name() == "test-scratch-ram") {
            REQUIRE_FALSE(info.has_ui());
            saw_test_scratch_ram = true;
        } else if (info.name() == "ui-bearing") {
            REQUIRE(info.has_ui());
            saw_ui_bearing = true;
        }
    }
    REQUIRE(saw_test_scratch_ram);
    REQUIRE(saw_ui_bearing);

    server.stop();
    registry.shutdown();
}

TEST_CASE("PeripheralExtensionService ListExtensions serialises storage devices",
          "[grpc][extension][discovery][storage]") {
    // Round-trip a StorageBearingTestExtension through the gRPC
    // service and assert all StorageDevice fields survive: id,
    // label, kind (both Fixed and Removable), media_type,
    // backing_path (including empty), activity_indicator_name.
    beebium::ModelB machine;
    machine.reset();

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    registry.register_extension(beebium::TestScratchRam::create());
    registry.register_extension(std::make_unique<StorageBearingTestExtension>());

    beebium::ExtensionContext ctx(&machine.state().memory.one_mhz_bus());
    registry.resolve_and_init(ctx);

    beebium::service::PeripheralExtensionServiceImpl peripheral_ext_service(registry);
    auto ext_services = registry.collect_grpc_services();
    ext_services.push_back(&peripheral_ext_service);

    beebium::service::Server<beebium::ModelB> server(machine, "127.0.0.1", 0);
    server.start({}, {}, false, {}, nullptr, ext_services);

    auto channel = grpc::CreateChannel(
        "127.0.0.1:" + std::to_string(server.port()),
        grpc::InsecureChannelCredentials());
    auto stub = beebium::PeripheralExtensionService::NewStub(channel);

    grpc::ClientContext grpc_ctx;
    beebium::ListExtensionsRequest request;
    beebium::ListExtensionsResponse response;

    auto status = stub->ListExtensions(&grpc_ctx, request, &response);
    REQUIRE(status.ok());

    const beebium::ExtensionInfo* storage_info = nullptr;
    const beebium::ExtensionInfo* scratch_info = nullptr;
    for (int i = 0; i < response.extensions_size(); ++i) {
        const auto& info = response.extensions(i);
        if (info.name() == "storage-bearing") {
            storage_info = &info;
        } else if (info.name() == "test-scratch-ram") {
            scratch_info = &info;
        }
    }
    REQUIRE(storage_info != nullptr);
    REQUIRE(scratch_info != nullptr);

    // TestScratchRam doesn't implement ExtensionStorage, so its
    // storage_devices array stays empty -- not all extensions
    // become storage devices just because the field exists.
    REQUIRE(scratch_info->storage_devices_size() == 0);

    // Storage-bearing extension published two devices in order.
    REQUIRE(storage_info->storage_devices_size() == 2);

    const auto& fixed = storage_info->storage_devices(0);
    REQUIRE(fixed.id() == "test-fixed");
    REQUIRE(fixed.name() == "Test Fixed Device");
    REQUIRE(fixed.kind() == beebium::StorageDevice::FIXED);
    REQUIRE(fixed.media_type() == "test-fixed-media");
    REQUIRE(fixed.backing_path() == "/tmp/fixed.dat");
    REQUIRE(fixed.activity_indicator_name() == "test-fixed-led");

    const auto& removable = storage_info->storage_devices(1);
    REQUIRE(removable.id() == "test-removable");
    REQUIRE(removable.kind() == beebium::StorageDevice::REMOVABLE);
    REQUIRE(removable.backing_path().empty());

    server.stop();
    registry.shutdown();
}

TEST_CASE("PeripheralExtensionService ListExtensions empty when no extensions loaded",
          "[grpc][extension][discovery]") {
    beebium::ModelB machine;
    machine.reset();

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");

    beebium::ExtensionContext ctx(&machine.state().memory.one_mhz_bus());
    registry.resolve_and_init(ctx);

    beebium::service::PeripheralExtensionServiceImpl peripheral_ext_service(registry);

    auto ext_services = registry.collect_grpc_services();
    ext_services.push_back(&peripheral_ext_service);

    beebium::service::Server<beebium::ModelB> server(machine, "127.0.0.1", 0);
    server.start({}, {}, false, {}, nullptr, ext_services);

    auto channel = grpc::CreateChannel(
        "127.0.0.1:" + std::to_string(server.port()),
        grpc::InsecureChannelCredentials());
    auto stub = beebium::PeripheralExtensionService::NewStub(channel);

    grpc::ClientContext grpc_ctx;
    beebium::ListExtensionsRequest request;
    beebium::ListExtensionsResponse response;

    auto status = stub->ListExtensions(&grpc_ctx, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.extensions_size() == 0);

    server.stop();
    registry.shutdown();
}
