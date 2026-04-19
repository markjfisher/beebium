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

// End-to-end gRPC tests for ExtensionUiService. Drives a fake extension
// (FakeUiTransport) loaded into an EconetTransportRegistry through the
// real service implementation, with no real machine state involved.
// Exercises the full validation gauntlet (NOT_FOUND, stale revision,
// unknown control id, payload-type mismatch) plus the streaming
// SubscribeView path with mark_dirty triggering a second push.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/econet/NetworkBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionUi.hpp"
#include "beebium/service/ExtensionUiService.hpp"
#include "beebium/service/Server.hpp"

#include "extension_ui.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {

// FakeUi exposes a single Toggle (id="toggle1") whose value is mutable
// from tests. handle_event records the last event so tests can assert
// that the framework reached it.
class FakeUi : public beebium::ExtensionUi {
public:
    void build_view(beebium::View* out) const override {
        auto* root = out->mutable_root();
        root->set_id("root");
        auto* group = root->mutable_group();
        auto* tc = group->add_controls();
        tc->set_id("toggle1");
        auto* t = tc->mutable_toggle();
        t->set_label("Test");
        t->set_value(value_.load(std::memory_order_acquire));
    }

    void handle_event(const beebium::DispatchRequest& req) override {
        last_control_id_ = req.control_id();
        if (req.payload_case() == beebium::DispatchRequest::kBoolValue) {
            value_.store(req.bool_value(), std::memory_order_release);
        }
        ++event_count_;
    }

    bool value() const { return value_.load(std::memory_order_acquire); }
    const std::string& last_control_id() const { return last_control_id_; }
    int event_count() const { return event_count_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> value_{false};
    std::string last_control_id_;
    std::atomic<int> event_count_{0};
};

// Minimal EconetTransportExtension subclass that exposes a FakeUi.
// create_backend() is never called because the tests don't fit Econet
// hardware on the machine -- the ExtensionUiService only cares about
// name() and ui().
class FakeUiTransport : public beebium::EconetTransportExtension {
public:
    explicit FakeUiTransport(std::string name = "fake") {
        beebium::ExtensionManifest m;
        m.name = std::move(name);
        m.description = "Fake UI extension for tests";
        m.cli_name = m.name;
        m.extension_kind = "econet-transport";
        set_manifest(std::move(m));
    }

    std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
        return nullptr;
    }

    beebium::ExtensionUi* ui() override { return &ui_; }
    FakeUi& fake_ui() { return ui_; }

private:
    FakeUi ui_;
};

// Spin up a Server with only ExtensionUiService bound to a registry
// containing the caller's transport extensions. The peripheral registry
// is left empty.
class ExtensionUiFixture {
public:
    explicit ExtensionUiFixture(beebium::EconetTransportRegistry& transports)
        : service_(transports, peripheral_registry_) {
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
        stub_ = beebium::ExtensionUiService::NewStub(channel_);
    }

    ~ExtensionUiFixture() {
        server_->stop();
    }

    beebium::ExtensionUiService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::ExtensionRegistry peripheral_registry_;
    beebium::service::ExtensionUiServiceImpl service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionUiService::Stub> stub_;
};

}  // namespace

TEST_CASE("SubscribeView for unknown extension fails NOT_FOUND immediately",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::SubscribeViewRequest req;
    req.set_extension_name("nonexistent");
    auto reader = fixture.stub().SubscribeView(&ctx, req);

    beebium::View view;
    REQUIRE_FALSE(reader->Read(&view));
    auto status = reader->Finish();
    REQUIRE(status.error_code() == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("SubscribeView pushes initial View, then a second after mark_dirty",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::SubscribeViewRequest req;
    req.set_extension_name("fake");
    auto reader = fixture.stub().SubscribeView(&ctx, req);

    beebium::View view;
    REQUIRE(reader->Read(&view));
    REQUIRE(view.extension_name() == "fake");
    REQUIRE(view.view_revision() == 1u);
    REQUIRE(view.root().id() == "root");
    REQUIRE(view.root().control_case() == beebium::Control::kGroup);
    REQUIRE(view.root().group().controls_size() == 1);
    REQUIRE(view.root().group().controls(0).id() == "toggle1");
    REQUIRE(view.root().group().controls(0).toggle().value() == false);

    fake_ptr->fake_ui().mark_dirty();

    REQUIRE(reader->Read(&view));
    REQUIRE(view.view_revision() == 2u);

    ctx.TryCancel();
    // Drain any final write the server may have queued before noticing
    // the cancel; ignore the result.
    while (reader->Read(&view)) {}
    (void)reader->Finish();
}

TEST_CASE("Dispatch on current revision with matching payload runs handle_event",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_name("fake");
    req.set_control_id("toggle1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_bool_value(true);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(resp.error().empty());
    REQUIRE(fake_ptr->fake_ui().value() == true);
    REQUIRE(fake_ptr->fake_ui().last_control_id() == "toggle1");
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
}

TEST_CASE("Dispatch with stale view_revision is rejected",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    // Bump the revision so the request below is stale by construction.
    fake_ptr->fake_ui().mark_dirty();
    REQUIRE(fake_ptr->fake_ui().current_revision() == 2u);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_name("fake");
    req.set_control_id("toggle1");
    req.set_view_revision(1);  // stale
    req.set_bool_value(true);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("stale") != std::string::npos);
    // handle_event must not have been called.
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
    REQUIRE(fake_ptr->fake_ui().value() == false);
}

TEST_CASE("Dispatch with unknown control id is rejected",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_name("fake");
    req.set_control_id("does-not-exist");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_bool_value(true);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown control") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch with wrong payload type for the addressed control is rejected",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_name("fake");
    req.set_control_id("toggle1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    // Wrong payload variant for a Toggle (which expects bool_value).
    req.set_string_value("not a bool");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("payload type") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Cancelling the SubscribeView context exits the server poll loop cleanly",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    registry.add(std::make_unique<FakeUiTransport>());
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::SubscribeViewRequest req;
    req.set_extension_name("fake");
    auto reader = fixture.stub().SubscribeView(&ctx, req);

    // Receive at least the initial push.
    beebium::View view;
    REQUIRE(reader->Read(&view));

    auto t0 = std::chrono::steady_clock::now();
    ctx.TryCancel();
    while (reader->Read(&view)) {}
    auto status = reader->Finish();
    auto elapsed = std::chrono::steady_clock::now() - t0;

    // Loop should exit within a few poll intervals (~50ms each); allow
    // generous slack for slow CI machines.
    REQUIRE(elapsed < std::chrono::seconds(2));
    // Cancelled streams report CANCELLED to the client; OK is also
    // acceptable if the server happened to write+exit cleanly between
    // cancel signal and next poll.
    REQUIRE((status.error_code() == grpc::StatusCode::CANCELLED ||
             status.error_code() == grpc::StatusCode::OK));
}
