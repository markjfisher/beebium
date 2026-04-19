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

constexpr const char* CONTROL_DEVICE_PATH = "device_path";
constexpr const char* CONTROL_CONNECTED   = "connected";
constexpr const char* CONTROL_MODE_TOGGLE = "mode_toggle";

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

    // Indicator: serial_->is_open() drives OK / ERROR.
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_CONNECTED);
        auto* indicator = control->mutable_indicator();
        const PiconetBackend* backend = ext_.backend();
        const bool open = backend && backend->is_connected();
        indicator->set_state(open ? Indicator_State_OK
                                  : Indicator_State_ERROR);
        indicator->set_text(open ? "Adapter responsive"
                                 : "Adapter offline");
    }

    // Toggle: LISTEN <-> STOP. Reads the cached mode, so the displayed
    // value reflects the last mode actually written to the firmware.
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_MODE_TOGGLE);
        auto* toggle = control->mutable_toggle();
        toggle->set_label("Enabled");
        const PiconetBackend* backend = ext_.backend();
        const bool listening = backend && backend->mode() == piconet::Mode::Listen;
        toggle->set_value(listening);
    }
}

void PiconetUi::handle_event(const DispatchRequest& request) {
    // The framework has already validated extension/control/payload; we
    // only need to dispatch on control_id. Unknown ids reach here only
    // if a future control was added without a matching branch -- silent
    // ignore is the correct fallback (no firmware effect).
    if (request.control_id() == CONTROL_MODE_TOGGLE) {
        if (auto* backend = ext_.backend()) {
            backend->set_mode(request.bool_value() ? piconet::Mode::Listen
                                                   : piconet::Mode::Stop);
            mark_dirty();
        }
    }
}

}  // namespace beebium
