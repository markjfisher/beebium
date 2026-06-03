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

#include "LoopbackSerialExtension.hpp"
#include "LoopbackSerialUi.hpp"

#include "extension_ui.pb.h"

#include <string>

using namespace beebium;

TEST_CASE("LoopbackSerialExtension::ui() returns a non-null UI",
          "[loopback-serial][ui]") {
    LoopbackSerialExtension ext;
    REQUIRE(ext.ui() != nullptr);
}

TEST_CASE("LoopbackSerialUi build_view emits a status label",
          "[loopback-serial][ui]") {
    LoopbackSerialExtension ext;

    View view;
    ext.ui()->build_view(&view);

    REQUIRE(view.root().control_case() == Control::kGroup);
    REQUIRE(view.root().group().controls_size() == 1);

    const auto& status = view.root().group().controls(0);
    REQUIRE(status.control_case() == Control::kLabel);
    REQUIRE(status.label().text().find("Loopback active") != std::string::npos);
}
