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

// test_preset_loader.cpp
// Tests for preset file loading and parsing (PresetLoader.hpp)

#include <beebium/server/PresetLoader.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace beebium::server;

// Test assets directory is defined via compile definition
#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

namespace {
    std::filesystem::path presets_dirpath() {
        return std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "presets";
    }
}

// ============================================================================
// normalize_image_uri() tests
// ============================================================================

TEST_CASE("normalize_image_uri: file:// URI unchanged", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("file:///path/to/game.ssd", "/some/dir");
    REQUIRE(result == "file:///path/to/game.ssd");
}

TEST_CASE("normalize_image_uri: library:// URI unchanged", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("library://games/Elite.ssd", "/some/dir");
    REQUIRE(result == "library://games/Elite.ssd");
}

TEST_CASE("normalize_image_uri: http:// URI unchanged", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("http://example.com/game.ssd", "/some/dir");
    REQUIRE(result == "http://example.com/game.ssd");
}

TEST_CASE("normalize_image_uri: absolute path to file:// URI", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("/path/to/game.ssd", "/some/dir");
    REQUIRE(result == "file:///path/to/game.ssd");
}

TEST_CASE("normalize_image_uri: relative path resolved from base dir", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("game.ssd", "/home/user/presets");
    REQUIRE(result == "file:///home/user/presets/game.ssd");
}

TEST_CASE("normalize_image_uri: relative path with subdirectory", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("discs/game.ssd", "/home/user/presets");
    REQUIRE(result == "file:///home/user/presets/discs/game.ssd");
}

TEST_CASE("normalize_image_uri: relative path with parent directory", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("../discs/game.ssd", "/home/user/presets");
    // The filesystem path resolution should normalize the ..
    REQUIRE(result == "file:///home/user/discs/game.ssd");
}

// ============================================================================
// load_preset() tests - successful loading
// ============================================================================

TEST_CASE("load_preset: valid minimal preset", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "minimal.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config.has_value());
    REQUIRE(result.config->name == "Minimal Preset");
    REQUIRE_FALSE(result.config->model.has_value());
    REQUIRE_FALSE(result.config->storage.has_value());
}

TEST_CASE("load_preset: valid storage section", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "storage.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config.has_value());
    REQUIRE(result.config->name == "Storage Test Preset");
    REQUIRE(result.config->model == "ModelB");

    REQUIRE(result.config->storage.has_value());
    const auto& storage = *result.config->storage;

    // FDC socket
    REQUIRE(storage.fdc_socket_id == "acorn-1770");
    REQUIRE(storage.fdc_socket_mode == "1770");

    // Floppy drives
    REQUIRE(storage.floppy_drives.size() == 2);
    REQUIRE(storage.floppy_drives.at(0).has_value());
    REQUIRE(*storage.floppy_drives.at(0) == "file:///tmp/test.ssd");
    REQUIRE_FALSE(storage.floppy_drives.at(1).has_value());  // Explicit null

    // Cassette
    REQUIRE(storage.cassette_image_uri == "file:///tmp/test.uef");
}

TEST_CASE("load_preset: relative paths are resolved", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "relative_paths.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->storage.has_value());

    const auto& storage = *result.config->storage;
    REQUIRE(storage.floppy_drives.size() == 2);

    // Drive 0: relative path "game.ssd"
    REQUIRE(storage.floppy_drives.at(0).has_value());
    std::string drive0_uri = *storage.floppy_drives.at(0);
    REQUIRE(drive0_uri.find("file://") == 0);
    REQUIRE(drive0_uri.find("presets/game.ssd") != std::string::npos);

    // Drive 1: relative path with parent directory
    REQUIRE(storage.floppy_drives.at(1).has_value());
    std::string drive1_uri = *storage.floppy_drives.at(1);
    REQUIRE(drive1_uri.find("file://") == 0);
    // The .. should be resolved
    REQUIRE(drive1_uri.find("discs/other.ssd") != std::string::npos);
}

TEST_CASE("load_preset: unknown keys are ignored (forward compatibility)", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "unknown_keys.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->name == "Unknown Keys Preset");
    REQUIRE(result.config->model == "model-b");
    REQUIRE(result.config->storage.has_value());
    REQUIRE(result.config->storage->fdc_socket_id == "acorn-1770");
}

TEST_CASE("load_preset: name falls back to filename stem", "[preset][load_preset]") {
    // Create a test preset without a name field
    std::filesystem::path temp_filepath = std::filesystem::temp_directory_path() / "no_name.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b"})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->name == "no_name.preset");  // Stem of filename

    std::filesystem::remove(temp_filepath);
}

// ============================================================================
// load_preset() tests - error handling
// ============================================================================

TEST_CASE("load_preset: missing file returns error", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "nonexistent.preset.beebium");

    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("Cannot open") != std::string::npos);
}

TEST_CASE("load_preset: invalid JSON returns error", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "invalid.json");

    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("Invalid JSON") != std::string::npos);
    REQUIRE(result.error.find("parse error") != std::string::npos);
}

TEST_CASE("load_preset: non-object JSON returns error", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "not_object.json");

    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("must contain a JSON object") != std::string::npos);
}

// ============================================================================
// PresetLoadResult tests
// ============================================================================

TEST_CASE("PresetLoadResult: success() matches config presence", "[preset][PresetLoadResult]") {
    PresetLoadResult success_result{PresetConfig{"Test", std::nullopt, std::nullopt}, ""};
    REQUIRE(success_result.success());
    REQUIRE(static_cast<bool>(success_result));

    PresetLoadResult error_result{std::nullopt, "Error message"};
    REQUIRE_FALSE(error_result.success());
    REQUIRE_FALSE(static_cast<bool>(error_result));
}

// ============================================================================
// PresetStorageConfig tests
// ============================================================================

TEST_CASE("PresetStorageConfig: default state", "[preset][PresetStorageConfig]") {
    PresetStorageConfig storage;

    REQUIRE_FALSE(storage.fdc_socket_id.has_value());
    REQUIRE_FALSE(storage.fdc_socket_mode.has_value());
    REQUIRE(storage.floppy_drives.empty());
    REQUIRE_FALSE(storage.cassette_image_uri.has_value());
}

// ============================================================================
// PresetConfig tests
// ============================================================================

TEST_CASE("PresetConfig: default state", "[preset][PresetConfig]") {
    PresetConfig config;

    REQUIRE(config.name.empty());
    REQUIRE_FALSE(config.model.has_value());
    REQUIRE_FALSE(config.storage.has_value());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("load_preset: empty storage section", "[preset][load_preset]") {
    std::filesystem::path temp_filepath = std::filesystem::temp_directory_path() / "empty_storage.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Empty Storage", "storage": {}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage.has_value());
    REQUIRE_FALSE(result.config->storage->fdc_socket_id.has_value());
    REQUIRE(result.config->storage->floppy_drives.empty());

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: fdc_socket without mode", "[preset][load_preset]") {
    std::filesystem::path temp_filepath = std::filesystem::temp_directory_path() / "fdc_no_mode.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "FDC No Mode", "storage": {"fdc_socket": {"id": "acorn-1770"}}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage->fdc_socket_id == "acorn-1770");
    REQUIRE_FALSE(result.config->storage->fdc_socket_mode.has_value());

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: floppy_drives with only drive 1", "[preset][load_preset]") {
    std::filesystem::path temp_filepath = std::filesystem::temp_directory_path() / "drive1_only.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Drive 1 Only", "storage": {"floppy_drives": [{"drive": 1, "image_uri": "file:///test.ssd"}]}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage->floppy_drives.size() == 1);
    REQUIRE(result.config->storage->floppy_drives.count(0) == 0);
    REQUIRE(result.config->storage->floppy_drives.count(1) == 1);
    REQUIRE(*result.config->storage->floppy_drives.at(1) == "file:///test.ssd");

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: fdc_socket id 'none'", "[preset][load_preset]") {
    std::filesystem::path temp_filepath = std::filesystem::temp_directory_path() / "fdc_none.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "No FDC", "storage": {"fdc_socket": {"id": "none"}}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage->fdc_socket_id == "none");

    std::filesystem::remove(temp_filepath);
}
