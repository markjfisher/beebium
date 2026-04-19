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

// Direct (non-gRPC) tests for PiconetUi. Drives a real
// PiconetEconetTransportExtension wired to a FakePiconetDevice on a PTY,
// then exercises the UI hooks the framework would call: build_view()
// produces the expected three-control tree, and handle_event() for the
// mode_toggle control writes SET_MODE STOP/LISTEN to the firmware fake.
//
// The end-to-end gRPC path is covered separately by the framework's
// own test_grpc_extension_ui_service.cpp -- this file focuses on the
// piconet-specific behaviour without re-testing the framework's
// validation gauntlet.

#include <catch2/catch_test_macros.hpp>

#include "PiconetEconetTransportExtension.hpp"
#include "PiconetUi.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include "extension_ui.pb.h"

#include "piconet/FakePiconetDeviceOnPty.hpp"

#include <chrono>
#include <memory>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Spin up the extension wired to a PTY-backed FakePiconetDevice.
// PiconetBackend opens the slave end via PosixSerialPort and starts its
// reader thread; on construction it sends SET_STATION + SET_MODE LISTEN
// which the fake records on its master end.
class PiconetUiFixture {
public:
    PiconetUiFixture() {
        fake_ = std::make_unique<beebium::piconet::test::FakePiconetDeviceOnPty>();
        REQUIRE(fake_->is_open());

        ext_ = std::make_unique<beebium::PiconetEconetTransportExtension>();
        ext_->set_config({{"device_path", fake_->slave_path()}});
        backend_owner_ = ext_->create_backend(/*station=*/1);
        REQUIRE(backend_owner_ != nullptr);

        // Wait for the constructor's initial SET_MODE LISTEN to round-trip
        // through the PTY into the fake's mode_ field. Bounded short
        // sleep -- 50ms is comfortably above the pumper thread's polling
        // cadence.
        wait_for_mode(beebium::piconet::Mode::Listen);
    }

    beebium::PiconetEconetTransportExtension& extension() { return *ext_; }
    beebium::piconet::test::FakePiconetDevice& fake_device() { return fake_->device(); }
    const std::string& slave_path() const { return fake_->slave_path(); }
    beebium::PiconetBackend& backend() {
        return *static_cast<beebium::PiconetBackend*>(backend_owner_.get());
    }

    // Block until the fake observes the requested mode, up to 500ms.
    // Returns true if observed, false on timeout.
    bool wait_for_mode(beebium::piconet::Mode mode) {
        auto deadline = std::chrono::steady_clock::now() + 500ms;
        while (std::chrono::steady_clock::now() < deadline) {
            if (fake_->device().mode() == mode) return true;
            std::this_thread::sleep_for(5ms);
        }
        return fake_->device().mode() == mode;
    }

private:
    std::unique_ptr<beebium::piconet::test::FakePiconetDeviceOnPty> fake_;
    std::unique_ptr<beebium::PiconetEconetTransportExtension> ext_;
    std::unique_ptr<beebium::NetworkBackend> backend_owner_;
};

}  // namespace

TEST_CASE("PiconetUi build_view produces Group(Label, Indicator, Toggle)",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    beebium::View view;
    ui->build_view(&view);

    const auto& root = view.root();
    REQUIRE(root.id() == "root");
    REQUIRE(root.control_case() == beebium::Control::kGroup);
    REQUIRE(root.group().label() == "Piconet");
    REQUIRE(root.group().controls_size() == 3);

    const auto& label = root.group().controls(0);
    REQUIRE(label.id() == "device_path");
    REQUIRE(label.control_case() == beebium::Control::kLabel);
    REQUIRE(label.label().text() ==
            std::string("Device: ") + fixture.slave_path());

    const auto& indicator = root.group().controls(1);
    REQUIRE(indicator.id() == "connected");
    REQUIRE(indicator.control_case() == beebium::Control::kIndicator);
    REQUIRE(indicator.indicator().state() == beebium::Indicator_State_OK);
    REQUIRE(indicator.indicator().text() == "Adapter responsive");

    const auto& toggle = root.group().controls(2);
    REQUIRE(toggle.id() == "mode_toggle");
    REQUIRE(toggle.control_case() == beebium::Control::kToggle);
    REQUIRE(toggle.toggle().label() == "Enabled");
    // Constructor put the firmware in LISTEN, so the toggle should reflect
    // 'enabled'.
    REQUIRE(toggle.toggle().value() == true);
}

TEST_CASE("PiconetUi mode_toggle false dispatches SET_MODE STOP",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    beebium::DispatchRequest req;
    req.set_extension_name("piconet");
    req.set_control_id("mode_toggle");
    req.set_view_revision(ui->current_revision());
    req.set_bool_value(false);

    ui->handle_event(req);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));
    REQUIRE(fixture.backend().mode() == beebium::piconet::Mode::Stop);
}

TEST_CASE("PiconetUi mode_toggle true dispatches SET_MODE LISTEN",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    // Start from STOP so we observe a real LISTEN transition.
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    beebium::DispatchRequest req;
    req.set_extension_name("piconet");
    req.set_control_id("mode_toggle");
    req.set_view_revision(ui->current_revision());
    req.set_bool_value(true);

    ui->handle_event(req);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Listen));
    REQUIRE(fixture.backend().mode() == beebium::piconet::Mode::Listen);
}

TEST_CASE("PiconetUi mode_toggle dispatch bumps the view revision",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    auto initial = ui->current_revision();

    beebium::DispatchRequest req;
    req.set_extension_name("piconet");
    req.set_control_id("mode_toggle");
    req.set_view_revision(initial);
    req.set_bool_value(false);

    ui->handle_event(req);
    REQUIRE(ui->current_revision() > initial);
}

TEST_CASE("PiconetUi build_view reflects the latest set_mode",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    fixture.backend().set_mode(beebium::piconet::Mode::Stop);

    beebium::View view;
    ui->build_view(&view);

    const auto& toggle = view.root().group().controls(2);
    REQUIRE(toggle.id() == "mode_toggle");
    REQUIRE(toggle.toggle().value() == false);
}
