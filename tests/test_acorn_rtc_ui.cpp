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

// Tests for AcornRtcUi. Pure helpers (format_datetime, format_layout)
// in the first half; build_view shape + worker-thread lifecycle smoke
// test in the second half.

#include <catch2/catch_test_macros.hpp>

#include <AcornRtcExtension.hpp>
#include <AcornRtcUi.hpp>
#include <Saf3019p.hpp>

#include "extension_ui.pb.h"

using namespace beebium;

// MARK: - format_datetime

TEST_CASE("format_datetime zero-pads single-digit fields",
          "[acorn-rtc][ui]") {
    REQUIRE(AcornRtcUi::format_datetime(1985, 3, 5, 9, 7) == "1985-03-05 09:07");
}

TEST_CASE("format_datetime preserves two-digit fields",
          "[acorn-rtc][ui]") {
    REQUIRE(AcornRtcUi::format_datetime(1985, 12, 31, 23, 59) == "1985-12-31 23:59");
}

TEST_CASE("format_datetime handles year 2000 and beyond",
          "[acorn-rtc][ui]") {
    REQUIRE(AcornRtcUi::format_datetime(2025, 5, 20, 14, 30) == "2025-05-20 14:30");
}

// MARK: - format_layout

TEST_CASE("format_layout describes 4-bit year in R1",
          "[acorn-rtc][ui]") {
    REQUIRE(AcornRtcUi::format_layout(RegisterLayout::FourBitYearInR1)
            == "4-bit year in R1 (Acorn original, 1981-1996)");
}

TEST_CASE("format_layout describes 7-bit year in R7",
          "[acorn-rtc][ui]") {
    REQUIRE(AcornRtcUi::format_layout(RegisterLayout::SevenBitYearInR7)
            == "7-bit year in R7 (L3 v1.26, 1981-2099)");
}

TEST_CASE("format_layout describes 7-bit year in R1+R5",
          "[acorn-rtc][ui]") {
    REQUIRE(AcornRtcUi::format_layout(RegisterLayout::SevenBitYearInR1R5)
            == "7-bit year in R1+R5 (BeebMaster Y2KFIX, 1981-2108)");
}

// MARK: - build_view

TEST_CASE("AcornRtcExtension::ui() returns a non-null UI",
          "[acorn-rtc][ui]") {
    AcornRtcExtension ext;
    REQUIRE(ext.ui() != nullptr);
}

TEST_CASE("AcornRtcUi build_view emits a Group containing a Time Label",
          "[acorn-rtc][ui]") {
    AcornRtcExtension ext;
    REQUIRE(ext.chip().initialise({1985, 3, 15, 10, 30}));

    View view;
    ext.ui()->build_view(&view);

    REQUIRE(view.root().control_case() == Control::kGroup);
    REQUIRE(view.root().group().controls_size() == 1);

    const auto& time_ctrl = view.root().group().controls(0);
    REQUIRE(time_ctrl.control_case() == Control::kLabel);
    // The text starts with "Time: " and contains the initialised date.
    // build_view calls advance_counters before reading, so the minute
    // field may or may not have ticked on between init and the assertion
    // -- but the date won't have flipped, so we check that prefix.
    REQUIRE(time_ctrl.label().text().rfind("Time: ", 0) == 0);
    REQUIRE(time_ctrl.label().text().find("1985-03-15") != std::string::npos);
}

TEST_CASE("AcornRtcUi build_view sets the register layout as a tooltip",
          "[acorn-rtc][ui]") {
    AcornRtcExtension ext;
    REQUIRE(ext.chip().initialise({1985, 3, 15, 10, 30}));

    View view;
    ext.ui()->build_view(&view);

    const auto& time_ctrl = view.root().group().controls(0);
    REQUIRE(time_ctrl.tooltip() == "4-bit year in R1 (Acorn original, 1981-1996)");
}

TEST_CASE("AcornRtcUi tooltip reflects the chip's current layout",
          "[acorn-rtc][ui]") {
    AcornRtcExtension ext;
    ext.chip().set_layout(RegisterLayout::SevenBitYearInR7);
    REQUIRE(ext.chip().initialise({2025, 5, 20, 14, 30}));

    View view;
    ext.ui()->build_view(&view);

    const auto& time_ctrl = view.root().group().controls(0);
    REQUIRE(time_ctrl.tooltip() == "7-bit year in R7 (L3 v1.26, 1981-2099)");
    REQUIRE(time_ctrl.label().text().find("2025-05-20") != std::string::npos);
}

// MARK: - worker lifecycle

TEST_CASE("AcornRtcExtension construct/destroy doesn't leak or hang the ticker",
          "[acorn-rtc][ui]") {
    // Smoke: the UI starts a background thread on construction that
    // bumps mark_dirty every 30s. Destruction must signal the thread
    // promptly via the condition variable so test teardown is fast,
    // not "join hangs for 30s." This test simply constructs and
    // destroys; if Catch2 returns within a few seconds it passed.
    for (int i = 0; i < 3; ++i) {
        AcornRtcExtension ext;
        REQUIRE(ext.ui() != nullptr);
    }
}
