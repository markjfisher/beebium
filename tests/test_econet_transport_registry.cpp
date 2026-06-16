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

// Tests for the EconetTransportRegistry container. The registry is
// intentionally unconstrained (no singleton) so future machine types
// with multiple ADLCs can hold multiple transports.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/EconetTransportExtension.hpp>
#include <beebium/extension/EconetTransportRegistry.hpp>
#include <beebium/econet/NetworkBackend.hpp>

using namespace beebium;

namespace {

// Minimal backend stub so the test transport can produce one.
class StubBackend : public NetworkBackend {
public:
    void send_frame(const NetworkFrame&) override {}
    std::optional<NetworkFrame> receive_frame() override { return std::nullopt; }
    bool is_connected() const override { return true; }
};

class StubTransport : public EconetTransportExtension {
public:
    explicit StubTransport(int marker = 0) : marker_(marker) {}
    int marker() const { return marker_; }
    std::unique_ptr<NetworkBackend> create_backend(uint8_t) override {
        return std::make_unique<StubBackend>();
    }

private:
    int marker_;
};

// Helper: produce a StubTransport with a manifest whose name is set,
// so the registry's id auto-assignment has something to base its
// candidate id on.
std::unique_ptr<StubTransport> make_named_stub(std::string_view manifest_name) {
    auto t = std::make_unique<StubTransport>();
    ExtensionManifest m;
    m.name = std::string(manifest_name);
    t->set_manifest(std::move(m));
    return t;
}

}  // namespace

TEST_CASE("EconetTransportRegistry: starts empty", "[econet][transport][registry]") {
    EconetTransportRegistry registry;
    REQUIRE(registry.empty());
    REQUIRE(registry.size() == 0);
}

TEST_CASE("EconetTransportRegistry: add() takes ownership and appends",
          "[econet][transport][registry]") {
    EconetTransportRegistry registry;
    registry.add(std::make_unique<StubTransport>(1));
    REQUIRE(registry.size() == 1);
    REQUIRE_FALSE(registry.empty());

    registry.add(std::make_unique<StubTransport>(2));
    REQUIRE(registry.size() == 2);
}

TEST_CASE("EconetTransportRegistry: preserves insertion order",
          "[econet][transport][registry]") {
    EconetTransportRegistry registry;
    registry.add(std::make_unique<StubTransport>(10));
    registry.add(std::make_unique<StubTransport>(20));
    registry.add(std::make_unique<StubTransport>(30));

    REQUIRE(registry.size() == 3);
    auto& exts = registry.extensions();
    REQUIRE(static_cast<StubTransport*>(exts[0].get())->marker() == 10);
    REQUIRE(static_cast<StubTransport*>(exts[1].get())->marker() == 20);
    REQUIRE(static_cast<StubTransport*>(exts[2].get())->marker() == 30);
}

TEST_CASE("EconetTransportRegistry: assigns id == manifest name for the first instance",
          "[econet][transport][registry][identity]") {
    EconetTransportRegistry registry;
    auto t = make_named_stub("aun");
    auto* ptr = t.get();
    registry.add(std::move(t));
    REQUIRE(ptr->id() == "aun");
}

TEST_CASE("EconetTransportRegistry: disambiguates multiple instances of the same type",
          "[econet][transport][registry][identity]") {
    // Future Acorn Econet Bridge machines will hold multiple
    // transports; this test makes sure their auto-ids stay distinct.
    EconetTransportRegistry registry;
    auto a = make_named_stub("aun");
    auto b = make_named_stub("aun");
    auto* pa = a.get();
    auto* pb = b.get();
    registry.add(std::move(a));
    registry.add(std::move(b));
    REQUIRE(pa->id() == "aun");
    REQUIRE(pb->id() == "aun-1");
}

TEST_CASE("EconetTransportRegistry: preserves an explicit user-set id",
          "[econet][transport][registry][identity]") {
    EconetTransportRegistry registry;
    auto t = make_named_stub("aun");
    t->set_config({{"id", "primary-link"}});
    auto* ptr = t.get();
    registry.add(std::move(t));
    REQUIRE(ptr->id() == "primary-link");
}

TEST_CASE("EconetTransportExtension default on_station_id_changed is a no-op",
          "[econet][transport][extension]") {
    StubTransport t;
    // Just verify it's callable without crashing.
    t.on_station_id_changed(42);
    SUCCEED();
}
