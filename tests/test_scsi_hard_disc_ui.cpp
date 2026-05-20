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

// Tests for ScsiHardDiscUi. Two halves: the format_capacity helper as a
// pure function over total-sector counts, and build_view's resulting
// View shape when the extension's total_sectors_ is set to different
// representative values.

#include <catch2/catch_test_macros.hpp>

#include <ScsiHardDiscExtension.hpp>
#include <ScsiHardDiscUi.hpp>

#include "extension_ui.pb.h"

using namespace beebium;

// MARK: - format_capacity

TEST_CASE("format_capacity returns 'No image' for zero sectors",
          "[scsi][hard-disc][ui]") {
    // A ScsiHardDiscExtension that wasn't configured with an image
    // initialises without installing a SCSI target; total_sectors
    // stays 0 and the row should say so honestly, not "0 KB".
    REQUIRE(ScsiHardDiscUi::format_capacity(0) == "No image");
}

TEST_CASE("format_capacity returns kilobytes for sub-megabyte images",
          "[scsi][hard-disc][ui]") {
    // 132 sectors = 4 cyl x 1 head x 33 spt x 256 B = 33,792 B = 33.8 KB
    REQUIRE(ScsiHardDiscUi::format_capacity(132) == "33.8 KB");
}

TEST_CASE("format_capacity returns whole megabytes without a trailing .0",
          "[scsi][hard-disc][ui]") {
    // ~4 MB: 15625 sectors x 256 = 4,000,000 B exactly
    REQUIRE(ScsiHardDiscUi::format_capacity(15625) == "4 MB");
}

TEST_CASE("format_capacity returns megabytes with one decimal place",
          "[scsi][hard-disc][ui]") {
    // 615 cyl x 4 heads x 33 spt = 81,180 sectors x 256 = 20,782,080 B
    //   = 20.78 MB -> "20.8 MB"
    REQUIRE(ScsiHardDiscUi::format_capacity(615u * 4u * 33u) == "20.8 MB");
}

TEST_CASE("format_capacity uses MB once bytes >= 1,000,000",
          "[scsi][hard-disc][ui]") {
    // Exactly 1,000,000 bytes = 3906.25 sectors; use 3907 sectors
    // (one extra block past the boundary) to stay on the MB side.
    REQUIRE(ScsiHardDiscUi::format_capacity(3907) == "1 MB");
}

// MARK: - build_view

namespace {

// Helper: build a ScsiHardDiscExtension with a synthetic total_sectors
// and config for the SCSI ID disambiguator. Skips init() (which would
// need a SCSI host adapter and an on-disk image); the UI only cares
// about the metadata exposed via accessors.
std::unique_ptr<ScsiHardDiscExtension> make_fake_disc(
        uint32_t total_sectors,
        const std::string& scsi_id_str = "0") {
    auto ext = ScsiHardDiscExtension::create();
    ext->set_config({{"id", "scsi-hdd-" + scsi_id_str},
                     {"scsi-id", scsi_id_str}});
    ext->set_total_sectors(total_sectors);
    return ext;
}

}  // namespace

TEST_CASE("ScsiHardDiscExtension::ui() returns a non-null UI",
          "[scsi][hard-disc][ui]") {
    auto ext = make_fake_disc(132);
    REQUIRE(ext->ui() != nullptr);
}

TEST_CASE("ScsiHardDiscUi build_view emits a Group containing a Capacity Label",
          "[scsi][hard-disc][ui]") {
    auto ext = make_fake_disc(615u * 4u * 33u);  // ~20.8 MB
    auto* ui = ext->ui();
    REQUIRE(ui != nullptr);

    View view;
    ui->build_view(&view);

    REQUIRE(view.root().control_case() == Control::kGroup);
    REQUIRE(view.root().group().controls_size() == 1);

    const auto& capacity_ctrl = view.root().group().controls(0);
    REQUIRE(capacity_ctrl.control_case() == Control::kLabel);
    REQUIRE(capacity_ctrl.label().text() == "Capacity: 20.8 MB");
}

TEST_CASE("ScsiHardDiscUi reflects current total_sectors on each build_view",
          "[scsi][hard-disc][ui]") {
    // build_view is a pure function of state; if total_sectors changes
    // between calls (e.g. a future image-swap feature), the next build
    // must reflect it. Today total_sectors is fixed at init, but we
    // exercise the read path so a future regression has a tripwire.
    auto ext = make_fake_disc(132);
    auto* ui = ext->ui();

    View view1;
    ui->build_view(&view1);
    REQUIRE(view1.root().group().controls(0).label().text() == "Capacity: 33.8 KB");

    ext->set_total_sectors(15625);  // 4 MB

    View view2;
    ui->build_view(&view2);
    REQUIRE(view2.root().group().controls(0).label().text() == "Capacity: 4 MB");
}

TEST_CASE("ScsiHardDiscUi build_view labels the 'No image' case",
          "[scsi][hard-disc][ui]") {
    auto ext = make_fake_disc(0);
    auto* ui = ext->ui();

    View view;
    ui->build_view(&view);

    REQUIRE(view.root().group().controls(0).label().text() == "Capacity: No image");
}
