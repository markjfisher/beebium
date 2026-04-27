// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Tests for the --sideways validation logic that detects user blunders
// against each machine variant's slot topology (GitHub issue #31).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <beebium/ModelBHardware.hpp>
#include <beebium/ModelBPlusHardware.hpp>
#include <beebium/ModelBRomRamBoardHardware.hpp>
#include <beebium/SlotTopology.hpp>
#include <beebium/server/CliArgParsers.hpp>
#include <beebium/server/SidewaysValidation.hpp>

#include <vector>

using beebium::ModelBHardware;
using beebium::ModelBPlusHardware;
using beebium::ModelBRomRamBoardHardware;
using beebium::SlotTopology;
using beebium::server::SidewaysConfig;
using beebium::server::SidewaysSlotType;
using beebium::server::validate_sideways_configs;
using Catch::Matchers::ContainsSubstring;

namespace {

SidewaysConfig rom_cfg(std::uint8_t slot, std::string image) {
    return SidewaysConfig{slot, SidewaysSlotType::Rom, std::move(image)};
}

SidewaysConfig ram_cfg(std::uint8_t slot, std::string image = "") {
    return SidewaysConfig{slot, SidewaysSlotType::Ram, std::move(image)};
}

SidewaysConfig empty_cfg(std::uint8_t slot) {
    return SidewaysConfig{slot, SidewaysSlotType::Empty, ""};
}

}  // namespace

// ============================================================================
// SlotTopology basics: each machine variant exposes the layout we expect.
// ============================================================================

TEST_CASE("ModelBHardware topology has 4 sockets with 4-way aliasing",
          "[sideways][topology][model_b]") {
    auto topo = ModelBHardware::slot_topology();
    REQUIRE(topo.has_aliasing);
    REQUIRE(topo.sockets.size() == 4);

    // Every socket on the Model B can hold ROM, RAM, or be empty (set at
    // start-up by --sideways), but the type cannot be changed at runtime --
    // on real hardware that is a power-off chip-swap operation.
    for (const auto& s : topo.sockets) {
        REQUIRE(s.supports_rom);
        REQUIRE(s.supports_ram);
        REQUIRE(s.supports_empty);
        REQUIRE_FALSE(s.runtime_configurable);
        REQUIRE(s.slots.size() == 4);
    }

    // Spot-check: IC52 -> {0, 4, 8, 12}; IC101 -> {3, 7, 11, 15}.
    REQUIRE(topo.sockets[0].label == "IC52");
    REQUIRE(topo.sockets[0].slots == std::vector<int>{0, 4, 8, 12});
    REQUIRE(topo.sockets[3].label == "IC101");
    REQUIRE(topo.sockets[3].slots == std::vector<int>{3, 7, 11, 15});

    // Every slot 0..15 exists on a stock Model B.
    for (int slot = 0; slot < 16; ++slot) {
        REQUIRE(topo.slot_exists(slot));
    }
}

TEST_CASE("ModelBPlusHardware topology omits slots 12 and 13 and is ROM-only",
          "[sideways][topology][model_b_plus]") {
    auto topo = ModelBPlusHardware::slot_topology();
    REQUIRE(topo.has_aliasing);  // pairs share a socket
    REQUIRE(topo.sockets.size() == 6);

    for (const auto& s : topo.sockets) {
        REQUIRE(s.supports_rom);
        REQUIRE_FALSE(s.supports_ram);
        REQUIRE_FALSE(s.supports_empty);
        REQUIRE_FALSE(s.runtime_configurable);
    }

    // The hardware does not wire any socket to slots 12 or 13.
    REQUIRE_FALSE(topo.slot_exists(12));
    REQUIRE_FALSE(topo.slot_exists(13));
}

TEST_CASE("Model B+ S13 link in default West position binds IC71 to slots 14, 15",
          "[sideways][topology][model_b_plus][links]") {
    // Default-constructed link state == S13 West (factory position).
    auto topo = ModelBPlusHardware::slot_topology();
    auto* ic71 = topo.find_socket_for_slot(15);
    REQUIRE(ic71 != nullptr);
    REQUIRE(ic71->label == "IC71");
    REQUIRE(ic71->slots == std::vector<int>{14, 15});

    // With S13 West, slots 0 and 1 have no socket assigned.
    REQUIRE_FALSE(topo.slot_exists(0));
    REQUIRE_FALSE(topo.slot_exists(1));
}

TEST_CASE("Model B+ S13 link in East position rebinds IC71 to slots 0, 1",
          "[sideways][topology][model_b_plus][links]") {
    ModelBPlusHardware::MotherboardLinks links;
    links.s13 = ModelBPlusHardware::MotherboardLinks::S13Position::East;

    auto topo = ModelBPlusHardware::slot_topology(links);
    auto* ic71 = topo.find_socket_for_slot(0);
    REQUIRE(ic71 != nullptr);
    REQUIRE(ic71->label == "IC71");
    REQUIRE(ic71->slots == std::vector<int>{0, 1});

    // With S13 East, slots 14 and 15 have no socket assigned.
    REQUIRE_FALSE(topo.slot_exists(14));
    REQUIRE_FALSE(topo.slot_exists(15));
}

TEST_CASE("Model B+ MotherboardLinks::parse accepts case-insensitive S13 values",
          "[sideways][topology][model_b_plus][links]") {
    ModelBPlusHardware::MotherboardLinks links;
    REQUIRE_FALSE(links.parse("s13", "east").has_value());
    REQUIRE(links.s13 == ModelBPlusHardware::MotherboardLinks::S13Position::East);
    REQUIRE_FALSE(links.parse("S13", "WEST").has_value());
    REQUIRE(links.s13 == ModelBPlusHardware::MotherboardLinks::S13Position::West);
}

TEST_CASE("Model B+ MotherboardLinks::parse rejects unknown link or value",
          "[sideways][topology][model_b_plus][links]") {
    ModelBPlusHardware::MotherboardLinks links;
    auto err1 = links.parse("s13", "north");
    REQUIRE(err1.has_value());
    REQUIRE_THAT(*err1, ContainsSubstring("S13"));
    REQUIRE_THAT(*err1, ContainsSubstring("west, east"));

    auto err2 = links.parse("s99", "anything");
    REQUIRE(err2.has_value());
    REQUIRE_THAT(*err2, ContainsSubstring("Unknown motherboard link"));
}

TEST_CASE("Model B has no configurable motherboard slot-mapping links",
          "[sideways][topology][model_b][links]") {
    ModelBHardware::MotherboardLinks links;
    auto err = links.parse("s13", "east");
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("no configurable slot-mapping links"));
}

TEST_CASE("Validation against Model B+ with S13 West rejects --sideways 0:rom",
          "[sideways][validate][model_b_plus][links]") {
    auto topo = ModelBPlusHardware::slot_topology();  // S13 = West (default)
    std::vector<SidewaysConfig> configs = {
        rom_cfg(0, "basic.rom"),  // slot 0 unwired with S13=West
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("slot 0 does not exist"));
}

TEST_CASE("Validation against Model B+ with S13 East accepts --sideways 0:rom",
          "[sideways][validate][model_b_plus][links]") {
    ModelBPlusHardware::MotherboardLinks links;
    links.s13 = ModelBPlusHardware::MotherboardLinks::S13Position::East;
    auto topo = ModelBPlusHardware::slot_topology(links);
    std::vector<SidewaysConfig> configs = {
        rom_cfg(0, "basic.rom"),  // slot 0 IS wired with S13=East
    };
    REQUIRE_FALSE(validate_sideways_configs(topo, configs).has_value());
}

TEST_CASE("ModelBRomRamBoardHardware topology has 16 independent slots",
          "[sideways][topology][model_b_romram]") {
    auto topo = ModelBRomRamBoardHardware::slot_topology();
    REQUIRE_FALSE(topo.has_aliasing);
    REQUIRE(topo.sockets.size() == 16);
    for (int slot = 0; slot < 16; ++slot) {
        const auto* s = topo.find_socket_for_slot(slot);
        REQUIRE(s != nullptr);
        REQUIRE(s->slots == std::vector<int>{slot});
        REQUIRE(s->supports_rom);
        REQUIRE(s->supports_ram);
        REQUIRE(s->supports_empty);
        REQUIRE(s->runtime_configurable);
    }
}

// ============================================================================
// Validation success path: well-formed configurations validate cleanly.
// ============================================================================

TEST_CASE("Validation accepts a typical Model B configuration",
          "[sideways][validate][model_b]") {
    auto topo = ModelBHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(15, "bbc-basic.rom"),    // -> IC101
        rom_cfg(13, "acorn-dfs.rom"),    // -> IC88
    };
    REQUIRE_FALSE(validate_sideways_configs(topo, configs).has_value());
}

TEST_CASE("Validation accepts repeated identical ROM in aliased slots",
          "[sideways][validate][model_b]") {
    // Specifying the same ROM image for two aliased slots is redundant but
    // semantically harmless -- both requests target the same physical chip.
    auto topo = ModelBHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(0, "user.rom"),
        rom_cfg(4, "user.rom"),  // same socket as slot 0
    };
    REQUIRE_FALSE(validate_sideways_configs(topo, configs).has_value());
}

TEST_CASE("Validation accepts an empty configuration list",
          "[sideways][validate]") {
    REQUIRE_FALSE(validate_sideways_configs(
        ModelBHardware::slot_topology(), {}).has_value());
    REQUIRE_FALSE(validate_sideways_configs(
        ModelBPlusHardware::slot_topology(), {}).has_value());
    REQUIRE_FALSE(validate_sideways_configs(
        ModelBRomRamBoardHardware::slot_topology(), {}).has_value());
}

// ============================================================================
// Reject case 1: slot does not exist on this machine variant.
// ============================================================================

TEST_CASE("Validation rejects nonexistent slot on Model B+",
          "[sideways][validate][model_b_plus]") {
    auto topo = ModelBPlusHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(12, "user.rom"),  // slot 12 is not wired on B+
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("slot 12 does not exist"));
    REQUIRE_THAT(*err, ContainsSubstring("Valid slots are"));
}

// ============================================================================
// Reject case 2: socket does not support the requested type.
// ============================================================================

TEST_CASE("Validation rejects RAM on Model B+ (sockets are ROM-only)",
          "[sideways][validate][model_b_plus]") {
    auto topo = ModelBPlusHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        ram_cfg(4),
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("IC44"));
    REQUIRE_THAT(*err, ContainsSubstring("RAM"));
    REQUIRE_THAT(*err, ContainsSubstring("ROM-only"));
}

TEST_CASE("Validation rejects Empty on Model B+ (sockets are ROM-only)",
          "[sideways][validate][model_b_plus]") {
    auto topo = ModelBPlusHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        empty_cfg(15),
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("IC71"));
    REQUIRE_THAT(*err, ContainsSubstring("empty"));
}

// ============================================================================
// Reject case 3: aliased-socket type conflict (Model B specifically).
// ============================================================================

TEST_CASE("Validation rejects ROM+RAM in aliased Model B slots",
          "[sideways][validate][model_b][aliasing]") {
    auto topo = ModelBHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(0, "user.rom"),  // -> IC52
        ram_cfg(12),             // -> IC52 (aliased)
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("IC52"));
    REQUIRE_THAT(*err, ContainsSubstring("aliased to slots 0, 4, 8, 12"));
    REQUIRE_THAT(*err, ContainsSubstring("conflicting type requests"));
}

TEST_CASE("Validation rejects ROM+Empty in aliased Model B slots",
          "[sideways][validate][model_b][aliasing]") {
    auto topo = ModelBHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(3, "basic.rom"),
        empty_cfg(15),  // -> IC101 (aliased)
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("IC101"));
    REQUIRE_THAT(*err, ContainsSubstring("conflicting type requests"));
}

// ============================================================================
// Reject case 4: aliased-socket ROM image conflict.
// ============================================================================

TEST_CASE("Validation rejects two different ROM images in aliased slots",
          "[sideways][validate][model_b][aliasing]") {
    auto topo = ModelBHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(2, "imageA.rom"),
        rom_cfg(6, "imageB.rom"),  // -> IC100 (aliased)
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("IC100"));
    REQUIRE_THAT(*err, ContainsSubstring("conflicting ROM image requests"));
}

// ============================================================================
// Reject case 5: same slot specified twice.
// ============================================================================

TEST_CASE("Validation rejects same slot specified twice with conflict",
          "[sideways][validate][duplicate]") {
    // Different types in the same slot is a degenerate aliased-socket
    // conflict and must be reported.
    auto topo = ModelBHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(12, "imageA.rom"),
        ram_cfg(12),
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("IC52"));
    REQUIRE_THAT(*err, ContainsSubstring("conflicting type requests"));
}

TEST_CASE("Validation flags pure duplicate slot specifications",
          "[sideways][validate][duplicate]") {
    // Even when both requests agree, specifying the same slot twice is
    // almost certainly a typo and should be flagged.
    auto topo = ModelBRomRamBoardHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(7, "imageA.rom"),
        rom_cfg(7, "imageA.rom"),
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("slot 7 specified 2 times"));
}

// ============================================================================
// Multiple problems are reported in a single error.
// ============================================================================

TEST_CASE("Validation reports multiple distinct problems in one message",
          "[sideways][validate]") {
    auto topo = ModelBPlusHardware::slot_topology();
    std::vector<SidewaysConfig> configs = {
        rom_cfg(12, "x.rom"),  // nonexistent slot
        ram_cfg(4),            // unsupported type
    };
    auto err = validate_sideways_configs(topo, configs);
    REQUIRE(err.has_value());
    REQUIRE_THAT(*err, ContainsSubstring("slot 12 does not exist"));
    REQUIRE_THAT(*err, ContainsSubstring("IC44"));
}
