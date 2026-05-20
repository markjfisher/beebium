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

// Test PluginLoader: manifest scanning, opt-in loading, and extension registration.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/PluginLoader.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>

#include <filesystem>

namespace {

// The plugin build directory is passed via compile definition
#ifdef BEEBIUM_PLUGIN_DIR
    const std::filesystem::path kPluginDirpath = BEEBIUM_PLUGIN_DIR;
#else
    const std::filesystem::path kPluginDirpath;
#endif

}  // namespace

TEST_CASE("PluginLoader scan_manifests finds manifests in directory",
          "[extension][plugin]") {
    if (kPluginDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kPluginDirpath);

    REQUIRE_FALSE(manifests.empty());

    auto* manifest = beebium::PluginLoader::find_manifest(manifests, "test-scratch-ram");
    REQUIRE(manifest != nullptr);
    REQUIRE(manifest->name == "test-scratch-ram");
    REQUIRE(manifest->library_stem == "test-scratch-ram");
    REQUIRE_FALSE(manifest->description.empty());
    // display_name is parsed from the manifest and used as the per-instance
    // label default by Extension::label() when no explicit label is given.
    REQUIRE(manifest->display_name == "Test Scratch RAM");
    // Manifests without an explicit extension_kind default to "peripheral".
    REQUIRE(manifest->extension_kind == "peripheral");
}

TEST_CASE("PluginLoader scan_manifests returns empty for non-existent directory",
          "[extension][plugin]") {
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests("/non/existent/path");
    REQUIRE(manifests.empty());
}

TEST_CASE("PluginLoader scan_manifests with MissingDirPolicy::Throw "
          "throws on non-existent directory",
          "[extension][plugin]") {
    beebium::PluginLoader loader;
    REQUIRE_THROWS_AS(
        loader.scan_manifests("/non/existent/path",
                              beebium::PluginLoader::MissingDirPolicy::Throw),
        std::runtime_error);
}

TEST_CASE("PluginLoader scan_manifests with MissingDirPolicy::Throw "
          "succeeds for existing empty directory",
          "[extension][plugin]") {
    auto tmp_dirpath = std::filesystem::temp_directory_path()
                       / "beebium_plugin_loader_empty_dir";
    std::filesystem::create_directories(tmp_dirpath);

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(
        tmp_dirpath, beebium::PluginLoader::MissingDirPolicy::Throw);
    REQUIRE(manifests.empty());

    std::filesystem::remove_all(tmp_dirpath);
}

TEST_CASE("PluginLoader find_manifest returns nullptr for unknown name",
          "[extension][plugin]") {
    std::vector<beebium::ExtensionManifest> manifests;
    manifests.push_back({
        .name = "test-scratch-ram",
        .description = "desc",
        .library_stem = "test-scratch-ram",
    });

    REQUIRE(beebium::PluginLoader::find_manifest(manifests, "unknown") == nullptr);
}

TEST_CASE("PluginLoader load_extension loads and registers extension",
          "[extension][plugin]") {
    if (kPluginDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kPluginDirpath);

    auto* manifest = beebium::PluginLoader::find_manifest(manifests, "test-scratch-ram");
    REQUIRE(manifest != nullptr);

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");

    auto loaded = loader.load_extension(*manifest);
    auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
    REQUIRE(peripheral != nullptr);
    (void)loaded.release();
    registry.register_extension(std::unique_ptr<beebium::PeripheralExtension>(peripheral));
    REQUIRE(registry.extension_count() == 1);

    beebium::OneMHzBusPort port;
    beebium::ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    REQUIRE(registry.init_count() == 1);
    REQUIRE(registry.extensions()[0]->name() == "test-scratch-ram");

    // Verify the extension is functional: write and read via the bus port.
    // TestScratchRam occupies 0x80-0x87 (Test Hardware range per Acorn App Note 003).
    port.write(0x80, 0xAB);
    REQUIRE(port.read(0x80) == 0xAB);
    REQUIRE(port.read(0x88) == 0xFF);  // unclaimed

    registry.shutdown();
}

TEST_CASE("PluginLoader loaded extension provides gRPC services",
          "[extension][plugin]") {
    if (kPluginDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kPluginDirpath);

    auto* manifest = beebium::PluginLoader::find_manifest(manifests, "test-scratch-ram");
    REQUIRE(manifest != nullptr);

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    auto loaded = loader.load_extension(*manifest);
    auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
    REQUIRE(peripheral != nullptr);
    (void)loaded.release();
    registry.register_extension(std::unique_ptr<beebium::PeripheralExtension>(peripheral));

    beebium::OneMHzBusPort port;
    beebium::ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    auto services = registry.collect_grpc_services();
    REQUIRE_FALSE(services.empty());

    registry.shutdown();
}
