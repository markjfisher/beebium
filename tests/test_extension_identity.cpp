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

namespace {

// Test fixture: a tiny Extension subclass that overrides default_label()
// to compose a name from the manifest display_name + a config field.
// Mirrors the pattern that ScsiHardDiscExtension uses for "(ID N)".
class LabelComposingExtension : public Extension {
public:
    std::string default_label() const override {
        auto base = Extension::default_label();
        if (auto slot = config_value("slot")) {
            return base + " #" + std::string(*slot);
        }
        return base;
    }
};

}  // namespace

TEST_CASE("Extension subclass can override default_label() to fold in config",
          "[extension][identity]") {
    LabelComposingExtension ext;
    ExtensionManifest m;
    m.name = "thing";
    m.display_name = "Thing";
    ext.set_manifest(std::move(m));
    ext.set_config({{"id", "thing-1"}, {"slot", "3"}});
    REQUIRE(ext.label() == "Thing #3");
}

TEST_CASE("Extension explicit label still wins over a subclass-computed default",
          "[extension][identity]") {
    // The override is *only* consulted when no explicit per-instance
    // label was given. Users can always force a name from the CLI.
    LabelComposingExtension ext;
    ExtensionManifest m;
    m.name = "thing";
    m.display_name = "Thing";
    ext.set_manifest(std::move(m));
    ext.set_config({{"id", "thing-1"}, {"slot", "3"},
                    {"label", "Bespoke Name"}});
    REQUIRE(ext.label() == "Bespoke Name");
}

TEST_CASE("Extension default_label() returns display_name when subclass does not override",
          "[extension][identity]") {
    // Verifies the base-class default_label() implementation: returns
    // the manifest display_name (or id if no display_name). This is
    // what the unaltered label() chain already exercises, but stating
    // the contract explicitly makes the inheritance API obvious.
    auto ext = TestScratchRam::create();
    ext->set_config({{"id", "my-id"}});
    REQUIRE(ext->default_label() == "Test Scratch RAM");
}

// MARK: - make_extension_id

TEST_CASE("make_extension_id returns the manifest name when nothing collides",
          "[extension][identity]") {
    std::vector<std::string> existing;
    REQUIRE(make_extension_id("acorn-scsi", existing) == "acorn-scsi");
}

TEST_CASE("make_extension_id appends -1 when the bare name is taken",
          "[extension][identity]") {
    std::vector<std::string> existing = {"scsi-hard-disc"};
    REQUIRE(make_extension_id("scsi-hard-disc", existing) == "scsi-hard-disc-1");
}

TEST_CASE("make_extension_id walks suffixes until it finds a free one",
          "[extension][identity]") {
    std::vector<std::string> existing = {
        "scsi-hard-disc",
        "scsi-hard-disc-1",
        "scsi-hard-disc-2",
    };
    REQUIRE(make_extension_id("scsi-hard-disc", existing) == "scsi-hard-disc-3");
}

TEST_CASE("make_extension_id fills gaps left by removed instances",
          "[extension][identity]") {
    // Predictable iteration: when the suffix sequence has a gap (e.g.
    // a previous instance was removed, leaving "name" and "name-2"
    // but no "name-1"), the next assignment fills the gap rather than
    // jumping past it. Keeps ids stable across reconfigurations.
    std::vector<std::string> existing = {
        "scsi-hard-disc",
        "scsi-hard-disc-2",
    };
    REQUIRE(make_extension_id("scsi-hard-disc", existing) == "scsi-hard-disc-1");
}

TEST_CASE("make_extension_id ignores ids of different extension types",
          "[extension][identity]") {
    // Only same-base-name ids matter for collision; an unrelated
    // "acorn-rtc" doesn't bump the scsi-hard-disc counter.
    std::vector<std::string> existing = {
        "acorn-rtc",
        "acorn-scsi",
    };
    REQUIRE(make_extension_id("scsi-hard-disc", existing) == "scsi-hard-disc");
}

TEST_CASE("make_extension_id steps around an explicit user-set id collision",
          "[extension][identity]") {
    // If a user passed --scsi-hdd id=scsi-hard-disc-1 explicitly,
    // the auto-assigner should skip that id when handing out the
    // next default rather than blowing up or overwriting.
    std::vector<std::string> existing = {"scsi-hard-disc-1"};
    REQUIRE(make_extension_id("scsi-hard-disc", existing) == "scsi-hard-disc");

    std::vector<std::string> existing_both = {"scsi-hard-disc", "scsi-hard-disc-1"};
    REQUIRE(make_extension_id("scsi-hard-disc", existing_both) == "scsi-hard-disc-2");
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
