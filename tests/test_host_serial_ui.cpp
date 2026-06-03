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

TEST_CASE("HostSerialUi build_view shows device config and status",
          "[host-serial][ui]") {
    HostSerialExtension ext;
    ext.set_config({{"mode", "device"}, {"path", "/dev/ttyUSB0"}, {"baud", "9600"}});

    View view;
    ext.ui()->build_view(&view);

    REQUIRE(view.root().control_case() == Control::kGroup);
    const auto& group = view.root().group();
    REQUIRE(group.controls_size() == 4);  // mode, path, baud, status

    CHECK(group.controls(0).label().text() == "Mode: device");
    CHECK(group.controls(1).label().text() == "Path: /dev/ttyUSB0");
    CHECK(group.controls(2).label().text() == "Baud: 9600");
    CHECK(group.controls(3).label().text().find("not connected") != std::string::npos);
}

TEST_CASE("HostSerialUi pty mode omits path and baud", "[host-serial][ui]") {
    HostSerialExtension ext;
    ext.set_config({{"mode", "pty"}});

    View view;
    ext.ui()->build_view(&view);

    const auto& group = view.root().group();
    REQUIRE(group.controls_size() == 2);  // mode, status
    CHECK(group.controls(0).label().text() == "Mode: pty");
    CHECK(group.controls(1).label().text().find("not connected") != std::string::npos);
}
