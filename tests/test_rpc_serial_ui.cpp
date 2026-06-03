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

#include "RpcSerialExtension.hpp"
#include "RpcSerialUi.hpp"

#include "extension_ui.pb.h"

#include <string>

using namespace beebium;

TEST_CASE("RpcSerialExtension::ui() returns a non-null UI", "[rpc-serial][ui]") {
    RpcSerialExtension ext;
    REQUIRE(ext.ui() != nullptr);
}

TEST_CASE("RpcSerialUi build_view reports the role and pending counts",
          "[rpc-serial][ui]") {
    RpcSerialExtension ext;  // not init()ed: both pending counts are 0

    View view;
    ext.ui()->build_view(&view);

    REQUIRE(view.root().control_case() == Control::kGroup);
    const auto& group = view.root().group();
    REQUIRE(group.controls_size() == 3);

    CHECK(group.controls(0).label().text().find("Client-driven") != std::string::npos);
    CHECK(group.controls(1).label().text().find("0 bytes") != std::string::npos);
    CHECK(group.controls(2).label().text().find("0 bytes") != std::string::npos);
}
