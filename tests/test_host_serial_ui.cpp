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

#include <catch2/catch_test_macros.hpp>

#include "HostSerialExtension.hpp"
#include "HostSerialUi.hpp"

#include "extension_ui.pb.h"

#include <string>

using namespace beebium;

TEST_CASE("HostSerialExtension::ui() returns a non-null UI", "[host-serial][ui]") {
    HostSerialExtension ext;
    REQUIRE(ext.ui() != nullptr);
}

TEST_CASE("HostSerialUi build_view exposes a device editor and a status indicator",
          "[host-serial][ui]") {
    HostSerialExtension ext;  // not init()ed: endpoint null -> default snapshot

    View view;
    ext.ui()->build_view(&view);

    REQUIRE(view.root().control_case() == Control::kGroup);
    const auto& group = view.root().group();
    // "Device" heading, path editor, baud line, and -- because no endpoint is
    // attached -- a connection indicator surfacing the error.
    REQUIRE(group.controls_size() == 4);

    // Mode heading: no endpoint -> empty mode -> "Device Mode".
    const auto& heading = group.controls(0);
    CHECK(heading.id() == "device_heading");
    CHECK(heading.control_case() == Control::kLabel);
    CHECK(heading.label().text() == "Device Mode");

    // Device ModalEditor: always editable; anchor shows the path on its own
    // line (no "Device:" prefix, no "@ baud" suffix -- those moved out).
    const auto& device = group.controls(1);
    REQUIRE(device.id() == "device");
    REQUIRE(device.control_case() == Control::kModalEditor);
    CHECK(device.modal_editor().editable());
    CHECK(device.modal_editor().anchor().label().text() == "(unset)");

    // Editor body: a path EditableChoice and a baud EditableChoice.
    const auto& editor = device.modal_editor().editor();
    REQUIRE(editor.control_case() == Control::kGroup);
    REQUIRE(editor.group().controls_size() == 2);
    // The port control is a dropdown (EditableChoice) when host ports are
    // discovered, or a plain TextInput when none are -- depends on the test
    // host, so accept either.
    const auto& path = editor.group().controls(0);
    CHECK(path.id() == "path");
    CHECK((path.control_case() == Control::kEditableChoice
           || path.control_case() == Control::kTextInput));

    // Baud is always a dropdown of standard rates.
    CHECK(editor.group().controls(1).id() == "baud");
    bool has_19200 = false;
    for (const auto& option : editor.group().controls(1).editable_choice().options()) {
        if (option == "19200") has_19200 = true;
    }
    CHECK(has_19200);

    // Baud shown on its own line below the path.
    const auto& baud_line = group.controls(2);
    CHECK(baud_line.id() == "baud_display");
    CHECK(baud_line.control_case() == Control::kLabel);
    CHECK(baud_line.label().text().find("baud") != std::string::npos);

    // Status indicator: no endpoint -> disconnected/error. Only present in the
    // not-open state; when the device is open the panel omits it entirely.
    const auto& connected = group.controls(3);
    REQUIRE(connected.id() == "connected");
    REQUIRE(connected.control_case() == Control::kIndicator);
    CHECK(connected.indicator().state() == Indicator_State_ERROR);
}
