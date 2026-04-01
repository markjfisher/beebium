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

#include <beebium/extension/ExtensionRegistry.hpp>
#include <TestScratchRam.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

// Minimal extension that records init/shutdown calls
struct TrackingExtension : PeripheralExtension {
    std::string name_;
    std::vector<std::string_view> attaches_;
    std::vector<std::string_view> provides_;
    bool inited = false;
    bool shut_down = false;
    int init_order = -1;
    static int init_counter;

    TrackingExtension(std::string name,
                      std::vector<std::string_view> attaches = {},
                      std::vector<std::string_view> provides = {})
        : name_(std::move(name))
        , attaches_(std::move(attaches))
        , provides_(std::move(provides)) {}

    std::string_view name() const override { return name_; }

    std::span<const std::string_view> attaches_to() const override {
        return attaches_;
    }

    std::span<const std::string_view> provides() const override {
        return provides_;
    }

    void init(ExtensionContext&) override {
        inited = true;
        init_order = init_counter++;
    }

    void shutdown() override {
        shut_down = true;
    }
};

int TrackingExtension::init_counter = 0;

}  // namespace

TEST_CASE("ExtensionRegistry inits extension with satisfied dependency",
          "[extension][registry]") {
    ExtensionRegistry registry;
    OneMHzBusPort port;
    ExtensionContext ctx(&port);

    registry.register_extension_point("1mhz-bus");
    auto ext = std::make_unique<TrackingExtension>("test", std::vector<std::string_view>{"1mhz-bus"});
    auto* ptr = ext.get();
    registry.register_extension(std::move(ext));

    registry.resolve_and_init(ctx);

    REQUIRE(ptr->inited);
    REQUIRE(registry.init_count() == 1);
}

TEST_CASE("ExtensionRegistry fails on unsatisfied dependency",
          "[extension][registry]") {
    ExtensionRegistry registry;
    OneMHzBusPort port;
    ExtensionContext ctx(&port);

    // No extension point registered -- dependency unsatisfied
    registry.register_extension(
        std::make_unique<TrackingExtension>("test", std::vector<std::string_view>{"1mhz-bus"}));

    REQUIRE_THROWS_AS(registry.resolve_and_init(ctx), std::runtime_error);
}

TEST_CASE("ExtensionRegistry inits provider before consumer",
          "[extension][registry]") {
    TrackingExtension::init_counter = 0;

    ExtensionRegistry registry;
    OneMHzBusPort port;
    ExtensionContext ctx(&port);

    registry.register_extension_point("1mhz-bus");

    // Consumer registered first, but should init second
    auto consumer = std::make_unique<TrackingExtension>(
        "consumer", std::vector<std::string_view>{"scsi"});
    auto* consumer_ptr = consumer.get();

    auto provider = std::make_unique<TrackingExtension>(
        "provider", std::vector<std::string_view>{"1mhz-bus"}, std::vector<std::string_view>{"scsi"});
    auto* provider_ptr = provider.get();

    registry.register_extension(std::move(consumer));
    registry.register_extension(std::move(provider));

    registry.resolve_and_init(ctx);

    REQUIRE(provider_ptr->inited);
    REQUIRE(consumer_ptr->inited);
    REQUIRE(provider_ptr->init_order < consumer_ptr->init_order);
}

TEST_CASE("ExtensionRegistry detects cycle", "[extension][registry]") {
    ExtensionRegistry registry;
    OneMHzBusPort port;
    ExtensionContext ctx(&port);

    // A needs B, B needs A
    registry.register_extension(std::make_unique<TrackingExtension>(
        "ext-a", std::vector<std::string_view>{"ext-b-bus"}, std::vector<std::string_view>{"ext-a-bus"}));
    registry.register_extension(std::make_unique<TrackingExtension>(
        "ext-b", std::vector<std::string_view>{"ext-a-bus"}, std::vector<std::string_view>{"ext-b-bus"}));

    REQUIRE_THROWS_AS(registry.resolve_and_init(ctx), std::runtime_error);
}

TEST_CASE("ExtensionRegistry shutdown in reverse init order",
          "[extension][registry]") {
    TrackingExtension::init_counter = 0;

    ExtensionRegistry registry;
    OneMHzBusPort port;
    ExtensionContext ctx(&port);

    registry.register_extension_point("1mhz-bus");

    auto ext1 = std::make_unique<TrackingExtension>(
        "first", std::vector<std::string_view>{"1mhz-bus"}, std::vector<std::string_view>{"bus-a"});
    auto ext2 = std::make_unique<TrackingExtension>(
        "second", std::vector<std::string_view>{"bus-a"});
    auto* ptr1 = ext1.get();
    auto* ptr2 = ext2.get();

    registry.register_extension(std::move(ext1));
    registry.register_extension(std::move(ext2));

    registry.resolve_and_init(ctx);
    REQUIRE(ptr1->init_order == 0);
    REQUIRE(ptr2->init_order == 1);

    // Shutdown should happen in reverse
    registry.shutdown();
    REQUIRE(ptr1->shut_down);
    REQUIRE(ptr2->shut_down);
}

TEST_CASE("ExtensionRegistry extension with no dependencies inits",
          "[extension][registry]") {
    ExtensionRegistry registry;
    ExtensionContext ctx;  // no ports

    auto ext = std::make_unique<TrackingExtension>("standalone");
    auto* ptr = ext.get();
    registry.register_extension(std::move(ext));

    registry.resolve_and_init(ctx);
    REQUIRE(ptr->inited);
}

TEST_CASE("ExtensionRegistry with TestScratchRam", "[extension][registry]") {
    ExtensionRegistry registry;
    OneMHzBusPort port;
    ExtensionContext ctx(&port);

    registry.register_extension_point("1mhz-bus");
    registry.register_extension(TestScratchRam::create());

    registry.resolve_and_init(ctx);

    REQUIRE(registry.init_count() == 1);
    REQUIRE(port.is_claimed(beebium::TestScratchRam::kBaseOffset));
    REQUIRE(port.is_claimed(beebium::TestScratchRam::kEndOffset));
    REQUIRE_FALSE(port.is_claimed(beebium::TestScratchRam::kEndOffset + 1));
}
