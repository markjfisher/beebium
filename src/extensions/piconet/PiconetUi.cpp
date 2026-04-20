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

#include "PiconetUi.hpp"

#include "PiconetEconetTransportExtension.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"

#include "extension_ui.pb.h"

#include <string>

namespace beebium {

namespace {

constexpr const char* CONTROL_DEVICE_PATH   = "device_path";
constexpr const char* CONTROL_CONNECTED     = "connected";
constexpr const char* CONTROL_ENABLE_ACTION = "enable_action";

}  // namespace

void PiconetUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();
    group->set_label("Piconet");

    // Device path label.
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_DEVICE_PATH);
        std::string text = "Device: ";
        if (auto path = ext_.config_value("device_path"); path && !path->empty()) {
            text.append(path->data(), path->size());
        } else {
            text += "(unknown)";
        }
        control->mutable_label()->set_text(std::move(text));
    }

    // Indicator: USB-level health -- is the adapter physically there
    // and the serial port open? Independent of LISTEN/STOP mode. This
    // distinguishes "USB unplugged" (red) from "muted via STOP mode"
    // (green USB OK + grey header Disconnected); both produce zero
    // traffic but the user acts on them differently. When the
    // underlying open() failed at startup, surface the OS error text
    // so the user can fix it (wrong path, permissions, device not
    // present).
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_CONNECTED);
        auto* indicator = control->mutable_indicator();
        const PiconetBackend* backend = ext_.backend();
        const bool serial_open = backend && backend->is_serial_open();
        indicator->set_state(serial_open ? Indicator_State_OK
                                         : Indicator_State_ERROR);
        if (serial_open) {
            indicator->set_text("Adapter responsive");
        } else if (!ext_.open_error_message().empty()) {
            indicator->set_text("Cannot open device: " +
                                ext_.open_error_message());
        } else {
            // Backend was constructed but the serial port has since
            // closed (read error / hot-unplug detection in
            // PiconetBackend's reader_loop). Without a recorded error
            // string we can't be more specific.
            indicator->set_text("Adapter offline");
        }
    }

    // Enable / Disable action button. Single Button whose label flips
    // with the current firmware mode. Symmetric with AunUi's
    // Connect/Disconnect button (same UX role: change "is the BBC in
    // the loop?"). Suppressed when there's no live backend OR when the
    // serial port has closed (hot-unplug, read error) -- in either
    // case set_mode would be a no-op because the firmware is
    // unreachable, so the affordance would be misleading. A future
    // Reconnect button would slot in here for both cases.
    if (auto* backend = ext_.backend(); backend && backend->is_serial_open()) {
        auto* control = group->add_controls();
        control->set_id(CONTROL_ENABLE_ACTION);
        auto* button = control->mutable_button();
        const bool listening = backend->mode() == piconet::Mode::Listen;
        button->set_label(listening ? "Disable" : "Enable");
        button->set_enabled(true);
    }
}

void PiconetUi::handle_event(const DispatchRequest& request) {
    // The framework has already validated extension/control/payload; we
    // only need to dispatch on control_id. Unknown ids reach here only
    // if a future control was added without a matching branch -- silent
    // ignore is the correct fallback (no firmware effect).
    if (request.control_id() == CONTROL_ENABLE_ACTION) {
        if (auto* backend = ext_.backend()) {
            const bool listening = backend->mode() == piconet::Mode::Listen;
            backend->set_mode(listening ? piconet::Mode::Stop
                                        : piconet::Mode::Listen);
            mark_dirty();
        }
    }
}

}  // namespace beebium
