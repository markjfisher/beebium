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

// Tests for ScsiHardDiscExtension's label() composition. Multiple SCSI
// HDDs configured on the same adapter need to render distinguishably
// in the macOS Peripherals sidebar, so the extension folds the
// scsi-id into its default_label() rather than relying on the
// manifest display_name alone.

#include <catch2/catch_test_macros.hpp>

#include <ScsiHardDiscExtension.hpp>

using namespace beebium;

TEST_CASE("ScsiHardDiscExtension default_label includes the SCSI ID",
          "[scsi][hard-disc][extension]") {
    auto ext = ScsiHardDiscExtension::create();
    ext->set_config({{"id", "scsi-hdd-0"},
                     {"scsi-id", "0"},
                     {"image", "/tmp/example.dat"}});
    // "Hard Disc (SCSI ID 0)" rather than "SCSI Hard Disc (ID 0)"
    // to avoid saying SCSI twice and to be explicit about what kind
    // of ID this number is. The row always renders under the parent
    // SCSI Bus section in the tree, so the leading "SCSI" is implied
    // by context.
    REQUIRE(ext->default_label() == "Hard Disc (SCSI ID 0)");
    REQUIRE(ext->label() == "Hard Disc (SCSI ID 0)");
}

TEST_CASE("ScsiHardDiscExtension default_label falls back to display_name "
          "when scsi-id absent",
          "[scsi][hard-disc][extension]") {
    // Defensive: the create() factory and the manifest both default
    // scsi-id to "0", so config should always carry the key. If it
    // ever doesn't, the row still renders something usable.
    auto ext = ScsiHardDiscExtension::create();
    ext->set_config({{"id", "scsi-hdd-orphan"}});
    REQUIRE(ext->label() == "SCSI Hard Disc");
}

TEST_CASE("ScsiHardDiscExtension explicit per-instance label wins",
          "[scsi][hard-disc][extension]") {
    auto ext = ScsiHardDiscExtension::create();
    ext->set_config({{"id", "scsi-hdd-0"},
                     {"scsi-id", "0"},
                     {"image", "/tmp/example.dat"},
                     {"label", "Boot disc"}});
    REQUIRE(ext->label() == "Boot disc");
}

TEST_CASE("ScsiHardDiscExtension distinguishes multiple instances",
          "[scsi][hard-disc][extension]") {
    // The motivating case: three drives at IDs 0/1/2 must each have
    // a distinct label so the sidebar shows them as separate rows.
    auto a = ScsiHardDiscExtension::create();
    a->set_config({{"id", "a"}, {"scsi-id", "0"}});

    auto b = ScsiHardDiscExtension::create();
    b->set_config({{"id", "b"}, {"scsi-id", "1"}});

    auto c = ScsiHardDiscExtension::create();
    c->set_config({{"id", "c"}, {"scsi-id", "2"}});

    REQUIRE(a->label() != b->label());
    REQUIRE(b->label() != c->label());
    REQUIRE(a->label() != c->label());
}
