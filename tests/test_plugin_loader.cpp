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
}

TEST_CASE("PluginLoader scan_manifests returns empty for non-existent directory",
          "[extension][plugin]") {
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests("/non/existent/path");
    REQUIRE(manifests.empty());
}

TEST_CASE("PluginLoader find_manifest returns nullptr for unknown name",
          "[extension][plugin]") {
    std::vector<beebium::ExtensionManifest> manifests;
    manifests.push_back({"test-scratch-ram", "desc", "test-scratch-ram", {}});

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

    loader.load_extension(*manifest, registry);
    REQUIRE(registry.extension_count() == 1);

    beebium::OneMHzBusPort port;
    beebium::ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    REQUIRE(registry.init_count() == 1);
    REQUIRE(registry.extensions()[0]->name() == "test-scratch-ram");

    // Verify the extension is functional: write and read via the bus port
    port.write(0x50, 0xAB);
    REQUIRE(port.read(0x50) == 0xAB);
    REQUIRE(port.read(0x58) == 0xFF);  // unclaimed

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
    loader.load_extension(*manifest, registry);

    beebium::OneMHzBusPort port;
    beebium::ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    auto services = registry.collect_grpc_services();
    REQUIRE_FALSE(services.empty());

    registry.shutdown();
}
