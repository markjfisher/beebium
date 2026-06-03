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

#include "HostSerialUi.hpp"

#include "HostSerialExtension.hpp"

#include <beebium/serial/SerialPortPickerControl.hpp>

#include "extension_ui.pb.h"

#include <algorithm>
#include <string>
#include <vector>

namespace beebium {

namespace {

constexpr const char* CONTROL_DEVICE    = "device";
constexpr const char* CONTROL_CONNECTED = "connected";

// Sub-control field ids inside the device ModalEditor's editor tree. Addressed
// via EditorCommit.field_id; the frontend reads the same ids when building the
// per-sub-control buffers during the popover session.
constexpr const char* FIELD_PATH = "path";
constexpr const char* FIELD_BAUD = "baud";

// Standard host serial line speeds offered in the baud dropdown. The current
// baud is inserted if it isn't already one of these, so it is always selectable.
std::vector<int> baud_options(int current) {
    std::vector<int> rates = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    if (current > 0 && std::find(rates.begin(), rates.end(), current) == rates.end()) {
        rates.push_back(current);
        std::sort(rates.begin(), rates.end());
    }
    return rates;
}

}  // namespace

void HostSerialUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();
    // No group label: the sidebar already names the extension ("Host Serial"),
    // so a second "Host Serial" heading inside the panel only repeats it.

    // build_view runs on the gRPC SubscribeView thread; the endpoint's device
    // state is mutated on the emulation thread. Read one atomic snapshot.
    serial::HostSerialEndpoint::UiSnapshot snap;
    if (auto* endpoint = ext_.endpoint()) {
        snap = endpoint->ui_snapshot();
    }

    // Mode heading for the path/baud lines below it: "PTY Mode" for a startup
    // pty, "Device Mode" for a real serial port.
    {
        auto* control = group->add_controls();
        control->set_id("device_heading");
        control->mutable_label()->set_text(snap.mode == "pty" ? "PTY Mode"
                                                              : "Device Mode");
    }

    // The path on its own line, as the editable anchor: click it to re-point
    // (the popover carries both the port picker and the baud rate). The baud
    // shows on its own line below; splitting them off the anchor stops the long
    // "path @ baud" string from wrapping awkwardly in a narrow sidebar.
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_DEVICE);
        auto* modal = control->mutable_modal_editor();
        modal->set_editable(true);
        modal->set_commit_role(ModalEditor_CommitRole_SAVE);
        modal->set_show_cancel(true);

        {
            auto* anchor = modal->mutable_anchor();
            anchor->set_id(CONTROL_DEVICE);
            anchor->mutable_label()->set_text(
                snap.device_path.empty() ? std::string("(unset)") : snap.device_path);
        }

        auto* editor = modal->mutable_editor();
        editor->set_id("device_editor");
        auto* editor_group = editor->mutable_group();

        // Serial port: a dropdown of discovered ports, or a plain field to type
        // a path when none are found (shared helper).
        serial::build_port_picker_control(editor_group->add_controls(), FIELD_PATH,
                                          "Serial port", snap.device_path,
                                          "/dev/cu.usbserial...");

        // Baud: an EditableChoice rendered as a dropdown of standard rates,
        // carrying the chosen rate as a string (single source of truth on
        // commit; no index<->rate mapping to keep in sync with handle_event).
        {
            auto* c = editor_group->add_controls();
            c->set_id(FIELD_BAUD);
            auto* ec = c->mutable_editable_choice();
            ec->set_label("Baud");
            ec->set_value(std::to_string(snap.baud));
            for (int rate : baud_options(snap.baud)) {
                *ec->add_options() = std::to_string(rate);
            }
        }
    }

    // Baud on its own line below the path. Display-only here; it is edited via
    // the path's popover (which carries the baud picker).
    {
        auto* control = group->add_controls();
        control->set_id("baud_display");
        control->mutable_label()->set_text(std::to_string(snap.baud) + " baud");
    }

    // Indicator only when there is something to act on. A working device is the
    // expected state and "Connected" would just be noise; an unopened device
    // earns its line by surfacing the OS error so the user can fix it.
    if (!snap.serial_open) {
        auto* control = group->add_controls();
        control->set_id(CONTROL_CONNECTED);
        auto* indicator = control->mutable_indicator();
        indicator->set_state(Indicator_State_ERROR);
        indicator->set_text(snap.open_error_message.empty()
                                ? "Disconnected"
                                : "Cannot open device: " + snap.open_error_message);
    }
}

void HostSerialUi::handle_event(const DispatchRequest& request) {
    if (request.control_id() != CONTROL_DEVICE) {
        return;  // unknown control: no-op (a future control added without a branch)
    }

    auto* endpoint = ext_.endpoint();
    if (!endpoint) {
        return;  // not initialised
    }

    std::string new_path;
    int new_baud = 0;
    for (const auto& field : request.editor_commit().fields()) {
        if (field.field_id() == FIELD_PATH) {
            new_path = field.string_value();
        } else if (field.field_id() == FIELD_BAUD) {
            try {
                new_baud = std::stoi(field.string_value());
            } catch (...) {
                new_baud = 0;
            }
        }
    }

    if (new_path.empty()) {
        mark_dirty();  // empty commit: redraw, no re-point
        return;
    }
    if (new_baud <= 0) {
        // Baud unparsed / unchanged: keep the current one.
        new_baud = endpoint->ui_snapshot().baud;
        if (new_baud <= 0) {
            new_baud = 19200;
        }
    }

    endpoint->request_reopen(std::move(new_path), new_baud);
    mark_dirty();
}

}  // namespace beebium
