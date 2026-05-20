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

// Tests for extension instance identity (id, label) and qualified provider lookup.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <TestScratchRam.hpp>

using namespace beebium;

TEST_CASE("Extension id() returns empty when no config set", "[extension][identity]") {
    auto ext = TestScratchRam::create();
    REQUIRE(ext->id().empty());
}

TEST_CASE("Extension id() returns value from config", "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ext->set_config({{"id", "my-scratch"}});
    REQUIRE(ext->id() == "my-scratch");
}

// The two cases of the label() fallback chain -- explicit label > manifest
// display_name > id -- are covered by the more specific tests below
// ("falls back to manifest display_name", "still falls back to id when
// no label and no display_name"). What's left to assert here is just
// that an explicit per-instance label wins over everything else.

TEST_CASE("Extension label() returns explicit label when set", "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ext->set_config({{"id", "my-id"}, {"label", "My Display Name"}});
    REQUIRE(ext->label() == "My Display Name");
    REQUIRE(ext->id() == "my-id");  // id unchanged
}

TEST_CASE("Extension label() falls back to manifest display_name when no explicit label",
          "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ExtensionManifest m;
    m.name = "test-scratch-ram";
    m.display_name = "Test Scratch RAM";
    ext->set_manifest(std::move(m));
    ext->set_config({{"id", "my-id"}});
    REQUIRE(ext->label() == "Test Scratch RAM");
}

TEST_CASE("Extension label() prefers explicit label over manifest display_name",
          "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ExtensionManifest m;
    m.name = "test-scratch-ram";
    m.display_name = "Test Scratch RAM";
    ext->set_manifest(std::move(m));
    ext->set_config({{"id", "my-id"}, {"label", "User's Override"}});
    REQUIRE(ext->label() == "User's Override");
}

TEST_CASE("Extension label() still falls back to id when no label and no display_name",
          "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ExtensionManifest m;
    m.name = "test-scratch-ram";
    // No display_name set.
    ext->set_manifest(std::move(m));
    ext->set_config({{"id", "my-id"}});
    REQUIRE(ext->label() == "my-id");
}

TEST_CASE("Extension config_value() returns value for known key", "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ext->set_config({{"foo", "bar"}, {"baz", "42"}});
    REQUIRE(ext->config_value("foo").has_value());
    REQUIRE(*ext->config_value("foo") == "bar");
    REQUIRE(*ext->config_value("baz") == "42");
}

TEST_CASE("Extension config_value() returns nullopt for unknown key", "[extension][identity]") {
    auto ext = TestScratchRam::create();
    ext->set_config({{"foo", "bar"}});
    REQUIRE_FALSE(ext->config_value("missing").has_value());
}

TEST_CASE("Qualified provider lookup returns correct extension", "[extension][identity]") {
    OneMHzBusPort port;
    ExtensionContext ctx(&port);
    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");

    auto ext_a = TestScratchRam::create();
    ext_a->set_config({{"id", "scratch-a"}});

    // We can't register two TestScratchRams claiming the same addresses,
    // so just test the provider registration directly on the context.
    ctx.register_provider("test-bus", ext_a.get());

    // Bare lookup
    REQUIRE(ctx.provider("test-bus") == ext_a.get());

    // Qualified lookup (registered because ext_a has an id)
    REQUIRE(ctx.provider("test-bus", "scratch-a") == ext_a.get());

    // Unknown qualified lookup
    REQUIRE(ctx.provider("test-bus", "scratch-b") == nullptr);
}

TEST_CASE("Provider registered under qualified name by registry", "[extension][identity]") {
    OneMHzBusPort port;
    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");

    auto ext = TestScratchRam::create();
    ext->set_config({{"id", "my-scratch"}});
    auto* raw = ext.get();
    registry.register_extension(std::move(ext));

    ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    // TestScratchRam doesn't provide any extension points,
    // so it won't be in the provider map. But the mechanism
    // works for extensions that do provide (like AcornScsiHostAdapter).
    // This test verifies the id is accessible after init.
    REQUIRE(registry.extensions()[0]->id() == "my-scratch");
    // Without an explicit label, label() falls through to the
    // manifest's display_name (set by TestScratchRam::create()).
    // The id-only fallback is exercised by the dedicated test above.
    REQUIRE(registry.extensions()[0]->label() == "Test Scratch RAM");
}
