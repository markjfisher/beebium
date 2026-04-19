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

// Tests for the common Extension base class. The base provides identity
// (manifest, config, id, label, name, description) and has no lifecycle.
// Specialised lifecycle methods live on the derived extension-point
// classes (PeripheralExtension, EconetTransportExtension, etc.).

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/Extension.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

using namespace beebium;

namespace {

// Minimal concrete subclass for unit testing the base. Real extension
// points provide their own (PeripheralExtension, EconetTransportExtension).
class TestExtension : public Extension {};

ExtensionManifest make_manifest(std::string name = "test-ext",
                                std::string description = "Test extension") {
    ExtensionManifest m;
    m.name = std::move(name);
    m.description = std::move(description);
    return m;
}

}  // namespace

TEST_CASE("Extension base: name() reads from manifest", "[extension][base]") {
    TestExtension ext;
    ext.set_manifest(make_manifest("my-ext"));
    REQUIRE(ext.name() == "my-ext");
}

TEST_CASE("Extension base: description() reads from manifest", "[extension][base]") {
    TestExtension ext;
    ext.set_manifest(make_manifest("x", "Extension X"));
    REQUIRE(ext.description() == "Extension X");
}

TEST_CASE("Extension base: id() empty before config set", "[extension][base]") {
    TestExtension ext;
    REQUIRE(ext.id().empty());
}

TEST_CASE("Extension base: id() returns config['id']", "[extension][base]") {
    TestExtension ext;
    ext.set_config({{"id", "instance-7"}});
    REQUIRE(ext.id() == "instance-7");
}

TEST_CASE("Extension base: label() falls back to id", "[extension][base]") {
    TestExtension ext;
    ext.set_config({{"id", "fallback-id"}});
    REQUIRE(ext.label() == "fallback-id");
}

TEST_CASE("Extension base: label() prefers explicit config['label']", "[extension][base]") {
    TestExtension ext;
    ext.set_config({{"id", "an-id"}, {"label", "Display Name"}});
    REQUIRE(ext.label() == "Display Name");
}

TEST_CASE("Extension base: config_value() returns known key", "[extension][base]") {
    TestExtension ext;
    ext.set_config({{"foo", "bar"}});
    REQUIRE(ext.config_value("foo").has_value());
    REQUIRE(*ext.config_value("foo") == "bar");
}

TEST_CASE("Extension base: config_value() returns nullopt for missing key",
          "[extension][base]") {
    TestExtension ext;
    REQUIRE_FALSE(ext.config_value("missing").has_value());
}

TEST_CASE("Extension base: config() returns full map", "[extension][base]") {
    TestExtension ext;
    ext.set_config({{"a", "1"}, {"b", "2"}});
    REQUIRE(ext.config().size() == 2);
    REQUIRE(ext.config().at("a") == "1");
    REQUIRE(ext.config().at("b") == "2");
}

TEST_CASE("ExtensionManifest::extension_kind defaults to peripheral",
          "[extension][manifest]") {
    ExtensionManifest m;
    REQUIRE(m.extension_kind == "peripheral");
}

TEST_CASE("ExtensionManifest::extension_kind can be set",
          "[extension][manifest]") {
    ExtensionManifest m;
    m.extension_kind = "econet-transport";
    REQUIRE(m.extension_kind == "econet-transport");
}
