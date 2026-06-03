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
    REQUIRE(group.controls_size() == 2);  // device editor + connection indicator

    // Device ModalEditor: always editable; anchor shows the (unset) device.
    const auto& device = group.controls(0);
    REQUIRE(device.id() == "device");
    REQUIRE(device.control_case() == Control::kModalEditor);
    CHECK(device.modal_editor().editable());
    CHECK(device.modal_editor().anchor().label().text().rfind("Device: ", 0) == 0);

    // Editor body: a path EditableChoice and a baud EditableChoice.
    const auto& editor = device.modal_editor().editor();
    REQUIRE(editor.control_case() == Control::kGroup);
    REQUIRE(editor.group().controls_size() == 2);
    CHECK(editor.group().controls(0).id() == "path");
    CHECK(editor.group().controls(0).control_case() == Control::kEditableChoice);
    CHECK(editor.group().controls(1).id() == "baud");

    bool has_19200 = false;
    for (const auto& option : editor.group().controls(1).editable_choice().options()) {
        if (option == "19200") has_19200 = true;
    }
    CHECK(has_19200);

    // Status indicator: no endpoint -> disconnected/error.
    const auto& connected = group.controls(1);
    REQUIRE(connected.id() == "connected");
    REQUIRE(connected.control_case() == Control::kIndicator);
    CHECK(connected.indicator().state() == Indicator_State_ERROR);
}
