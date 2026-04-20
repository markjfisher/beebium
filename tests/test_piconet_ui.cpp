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

    // Simulate USB hot-unplug by destroying the fake (closes the PTY
    // master), which causes the slave fd PiconetBackend holds to see
    // EOF/HUP on the next read. PiconetBackend's reader thread then
    // closes its serial port, flipping is_serial_open() to false.
    void simulate_hot_unplug() { fake_.reset(); }

    // Block until is_serial_open() returns false, up to 3 seconds.
    // Used after simulate_hot_unplug() to wait for the reader thread
    // to react. Returns true if observed, false on timeout.
    bool wait_for_serial_close() {
        auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!backend().is_serial_open()) return true;
            std::this_thread::sleep_for(20ms);
        }
        return !backend().is_serial_open();
    }

private:
    std::unique_ptr<beebium::piconet::test::FakePiconetDeviceOnPty> fake_;
    std::unique_ptr<beebium::PiconetEconetTransportExtension> ext_;
    std::unique_ptr<beebium::NetworkBackend> backend_owner_;
};

}  // namespace

TEST_CASE("PiconetUi build_view (live backend) produces Label + Indicator + Button",
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

    const auto& button = root.group().controls(2);
    REQUIRE(button.id() == "enable_action");
    REQUIRE(button.control_case() == beebium::Control::kButton);
    REQUIRE(button.button().enabled());
    // Constructor put the firmware in LISTEN, so the action that the
    // user can take next is "Disable" (mute).
    REQUIRE(button.button().label() == "Disable");
}

TEST_CASE("PiconetUi build_view (no backend) hides Button and surfaces OS error",
          "[piconet][ui]") {
    // Construct an extension whose create_backend will fail at the
    // POSIX open() because the device path doesn't exist. This is the
    // realistic "user got the path wrong / device unplugged at startup"
    // case that motivated the polish.
    beebium::PiconetEconetTransportExtension ext;
    ext.set_config({{"device_path", "/dev/does-not-exist-piconet"}});
    auto backend = ext.create_backend(/*station=*/1);
    REQUIRE(backend == nullptr);
    REQUIRE_FALSE(ext.open_error_message().empty());

    auto* ui = ext.ui();
    REQUIRE(ui != nullptr);

    beebium::View view;
    ui->build_view(&view);

    const auto& root = view.root();
    REQUIRE(root.control_case() == beebium::Control::kGroup);
    // Two controls only -- Label + Indicator. Enable button suppressed
    // because there is no backend to drive.
    REQUIRE(root.group().controls_size() == 2);

    const auto& label = root.group().controls(0);
    REQUIRE(label.id() == "device_path");
    REQUIRE(label.control_case() == beebium::Control::kLabel);
    REQUIRE(label.label().text() == "Device: /dev/does-not-exist-piconet");

    const auto& indicator = root.group().controls(1);
    REQUIRE(indicator.id() == "connected");
    REQUIRE(indicator.control_case() == beebium::Control::kIndicator);
    REQUIRE(indicator.indicator().state() == beebium::Indicator_State_ERROR);
    // The exact strerror text varies by platform, but it should contain
    // some recognisable substring of the OS-level diagnosis. macOS and
    // Linux both produce "No such file or directory" for ENOENT.
    REQUIRE(indicator.indicator().text().find("Cannot open device") !=
            std::string::npos);
    REQUIRE(indicator.indicator().text().find("No such file") !=
            std::string::npos);
}

TEST_CASE("PiconetUi enable_action toggles mode between LISTEN and STOP",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    // Constructor leaves us in LISTEN; first dispatch should mute.
    REQUIRE(fixture.backend().mode() == beebium::piconet::Mode::Listen);

    beebium::DispatchRequest req;
    req.set_extension_name("piconet");
    req.set_control_id("enable_action");
    req.set_view_revision(ui->current_revision());
    // Buttons take no payload.

    ui->handle_event(req);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    // Second dispatch should re-enable.
    req.set_view_revision(ui->current_revision());
    ui->handle_event(req);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Listen));
}

TEST_CASE("PiconetUi enable_action label flips with the cached mode",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();

    // After SET_MODE STOP, the next View should show Enable as the
    // available action.
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    beebium::View view;
    ui->build_view(&view);
    const auto& button = view.root().group().controls(2);
    REQUIRE(button.id() == "enable_action");
    REQUIRE(button.button().label() == "Enable");
}

TEST_CASE("PiconetUi enable_action dispatch bumps the view revision",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    auto initial = ui->current_revision();

    beebium::DispatchRequest req;
    req.set_extension_name("piconet");
    req.set_control_id("enable_action");
    req.set_view_revision(initial);

    ui->handle_event(req);
    REQUIRE(ui->current_revision() > initial);
}

TEST_CASE("PiconetBackend is_connected requires both serial open AND mode LISTEN",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    REQUIRE(fixture.backend().is_serial_open());
    // Constructor leaves us in LISTEN with serial open -> connected.
    REQUIRE(fixture.backend().is_connected());

    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));
    // Serial still open, but mode != Listen, so not "connected" in the
    // user-meaningful sense (BBC is muted from the wire).
    REQUIRE(fixture.backend().is_serial_open());
    REQUIRE_FALSE(fixture.backend().is_connected());

    fixture.backend().set_mode(beebium::piconet::Mode::Listen);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Listen));
    REQUIRE(fixture.backend().is_connected());
}

TEST_CASE("PiconetBackend hot-unplug closes serial and updates the UI",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(fixture.backend().is_serial_open());
    REQUIRE(fixture.backend().is_connected());
    const auto pre_unplug_revision = ui->current_revision();

    // Yank the cable: destroying the fake closes the PTY master, the
    // slave PiconetBackend holds sees EOF on the next read, the reader
    // thread closes the serial port and exits.
    fixture.simulate_hot_unplug();
    REQUIRE(fixture.wait_for_serial_close());
    REQUIRE_FALSE(fixture.backend().is_serial_open());
    REQUIRE_FALSE(fixture.backend().is_connected());

    // The reader thread fires the on_async_state_change callback after
    // closing the serial port; PiconetEconetTransportExtension wires
    // it to ui_.mark_dirty(). Without this the framework's poll loop
    // would never push a new View and the panel would stay frozen
    // showing "Adapter responsive" + Disable button forever.
    REQUIRE(ui->current_revision() > pre_unplug_revision);

    // The Indicator now reads as ERROR with the "Adapter offline"
    // text (no recorded open_error_message because the open did
    // succeed initially -- the adapter went away later).
    beebium::View view;
    ui->build_view(&view);
    const auto& indicator = view.root().group().controls(1);
    REQUIRE(indicator.id() == "connected");
    REQUIRE(indicator.indicator().state() == beebium::Indicator_State_ERROR);
    REQUIRE(indicator.indicator().text() == "Adapter offline");

    // The Enable button is suppressed -- nothing to drive any more.
    REQUIRE(view.root().group().controls_size() == 2);
}
