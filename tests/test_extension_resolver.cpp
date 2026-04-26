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

// Test ExtensionResolver: merging multiple manifest sources with later-wins precedence.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/ExtensionManifest.hpp>
#include <beebium/extension/ExtensionResolver.hpp>

namespace {

beebium::ExtensionManifest make_manifest(std::string name, std::string cli = "") {
    beebium::ExtensionManifest m;
    m.name = std::move(name);
    m.cli_name = std::move(cli);
    m.extension_kind = "peripheral";
    return m;
}

}  // namespace

TEST_CASE("ExtensionResolver with single source exposes all manifests",
          "[extension][resolver]") {
    auto a = make_manifest("alpha");
    auto b = make_manifest("beta");
    std::vector<beebium::ExtensionManifest> source{a, b};

    beebium::ExtensionResolver resolver;
    resolver.add_source("test-source", source);

    REQUIRE(resolver.entries().size() == 2);
    auto map = resolver.cli_name_map();
    REQUIRE(map.contains("--alpha"));
    REQUIRE(map.contains("--beta"));
}

TEST_CASE("ExtensionResolver uses cli_name override when set",
          "[extension][resolver]") {
    auto m = make_manifest("acorn-65c02-coprocessor", "tube-65c02");
    std::vector<beebium::ExtensionManifest> source{m};

    beebium::ExtensionResolver resolver;
    resolver.add_source("built-in", source);

    auto map = resolver.cli_name_map();
    REQUIRE(map.contains("--tube-65c02"));
    REQUIRE_FALSE(map.contains("--acorn-65c02-coprocessor"));
}

TEST_CASE("ExtensionResolver later source overrides earlier for same cli_name",
          "[extension][resolver]") {
    auto stock = make_manifest("acorn-scsi");
    stock.description = "stock";
    auto user = make_manifest("acorn-scsi");
    user.description = "user-override";

    std::vector<beebium::ExtensionManifest> built_in{stock};
    std::vector<beebium::ExtensionManifest> user_dir{user};

    beebium::ExtensionResolver resolver;
    resolver.add_source("built-in", built_in);
    resolver.add_source("user-dir", user_dir);

    REQUIRE(resolver.entries().size() == 1);
    auto map = resolver.cli_name_map();
    REQUIRE(map.at("--acorn-scsi")->description == "user-override");
}

TEST_CASE("ExtensionResolver tracks source label per resolved manifest",
          "[extension][resolver]") {
    auto m1 = make_manifest("alpha");
    auto m2 = make_manifest("beta");
    auto m1b = make_manifest("alpha");

    std::vector<beebium::ExtensionManifest> first{m1, m2};
    std::vector<beebium::ExtensionManifest> second{m1b};

    beebium::ExtensionResolver resolver;
    resolver.add_source("first", first);
    resolver.add_source("second", second);

    REQUIRE(resolver.entries().size() == 2);

    std::map<std::string, std::string> source_by_name;
    for (const auto& e : resolver.entries()) {
        source_by_name[e.manifest->name] = e.source_label;
    }
    REQUIRE(source_by_name.at("alpha") == "second");
    REQUIRE(source_by_name.at("beta") == "first");
}

TEST_CASE("ExtensionResolver cli_name_map keys are lowercase with -- prefix",
          "[extension][resolver]") {
    auto m = make_manifest("MixedCase");
    std::vector<beebium::ExtensionManifest> source{m};

    beebium::ExtensionResolver resolver;
    resolver.add_source("test-source", source);

    auto map = resolver.cli_name_map();
    REQUIRE(map.contains("--mixedcase"));
    REQUIRE_FALSE(map.contains("--MixedCase"));
}

TEST_CASE("ExtensionResolver find_by_name returns winning manifest by canonical name",
          "[extension][resolver]") {
    auto stock = make_manifest("acorn-scsi");
    stock.description = "stock";
    auto user = make_manifest("acorn-scsi");
    user.description = "user-override";

    std::vector<beebium::ExtensionManifest> built_in{stock};
    std::vector<beebium::ExtensionManifest> user_dir{user};

    beebium::ExtensionResolver resolver;
    resolver.add_source("built-in", built_in);
    resolver.add_source("user-dir", user_dir);

    const auto* m = resolver.find_by_name("acorn-scsi");
    REQUIRE(m != nullptr);
    REQUIRE(m->description == "user-override");

    REQUIRE(resolver.find_by_name("nonexistent") == nullptr);
}

TEST_CASE("ExtensionResolver later source can override built-in entry",
          "[extension][resolver]") {
    // Simulates the use case in #37: a user-supplied --extension-dir
    // ships an extension with the same cli_name as a built-in. The user
    // version must win so built-ins can be replaced.
    auto built_in_tube = make_manifest("acorn-65c02-coprocessor", "tube-65c02");
    built_in_tube.description = "stock 65C02 3 MHz";
    auto user_tube = make_manifest("acorn-65c02-coprocessor", "tube-65c02");
    user_tube.description = "experimental 65C102 4 MHz";

    std::vector<beebium::ExtensionManifest> built_ins{built_in_tube};
    std::vector<beebium::ExtensionManifest> user_dir{user_tube};

    beebium::ExtensionResolver resolver;
    resolver.add_source("built-in", built_ins);
    resolver.add_source("user-dir", user_dir);

    auto map = resolver.cli_name_map();
    REQUIRE(map.at("--tube-65c02")->description == "experimental 65C102 4 MHz");
}
