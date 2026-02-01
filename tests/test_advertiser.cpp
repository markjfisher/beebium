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

#include <catch2/catch_test_macros.hpp>
#include <beebium/discovery/Advertiser.hpp>

#include <chrono>
#include <thread>

using namespace beebium::discovery;

TEST_CASE("create_advertiser returns non-null", "[advertiser]") {
    auto advertiser = create_advertiser();
    REQUIRE(advertiser != nullptr);
}

TEST_CASE("Advertiser initial state", "[advertiser]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();

    SECTION("Not advertising initially") {
        REQUIRE_FALSE(state.advertising);
    }

    SECTION("Advertised name is empty initially") {
        REQUIRE(state.actual_name.empty());
    }
}

TEST_CASE("Advertiser stop when not started is safe", "[advertiser]") {
    auto advertiser = create_advertiser();
    // Should not crash or throw
    advertiser->stop();
    auto state = advertiser->state();
    REQUIRE_FALSE(state.advertising);
}

TEST_CASE("Advertiser start when unavailable returns false", "[advertiser]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();

    if (!state.available) {
        // NullAdvertiser or unavailable platform
        ServiceInfo info;
        info.instance_name = "Test Machine";
        info.port = 48875;

        bool result = advertiser->start(info);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(advertiser->state().advertising);
    }
}

#ifdef __APPLE__
TEST_CASE("BonjourAdvertiser is available on macOS", "[advertiser][macos]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();
    REQUIRE(state.available);
}

// Helper to wait for advertising to become active (async callback)
static bool wait_for_advertising(Advertiser& advertiser, int timeout_ms = 1000) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (advertiser.state().advertising) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }
    return false;
}

TEST_CASE("BonjourAdvertiser can start and stop", "[advertiser][macos]") {
    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Beebium Test";
    info.port = 48875;
    info.txt_records["uuid"] = "test-uuid-1234";
    info.txt_records["model"] = "model-b";

    SECTION("Start advertising") {
        bool result = advertiser->start(info);
        REQUIRE(result);

        // Wait for async registration callback
        REQUIRE(wait_for_advertising(*advertiser));

        auto state = advertiser->state();
        REQUIRE(state.advertising);
        REQUIRE_FALSE(state.actual_name.empty());
    }

    SECTION("Stop advertising") {
        advertiser->start(info);
        wait_for_advertising(*advertiser);
        advertiser->stop();

        auto state = advertiser->state();
        REQUIRE_FALSE(state.advertising);
    }

    SECTION("Restart advertising") {
        advertiser->start(info);
        wait_for_advertising(*advertiser);
        advertiser->stop();

        bool result = advertiser->start(info);
        REQUIRE(result);
        REQUIRE(wait_for_advertising(*advertiser));
    }
}
#endif

#ifdef _WIN32
TEST_CASE("WindowsAdvertiser is available on Windows 10+", "[advertiser][windows]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();
    // The API is available, though the service may not be running in all environments
    REQUIRE(state.available);
}

// Helper to wait for advertising to become active (async callback)
static bool wait_for_advertising_windows(Advertiser& advertiser, int timeout_ms = 2000) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (advertiser.state().advertising) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }
    return false;
}

// Helper to check if mDNS is functional (may not be on CI environments)
static bool is_mdns_functional() {
    auto advertiser = create_advertiser();
    ServiceInfo info;
    info.instance_name = "mDNS Test";
    info.port = 12345;
    bool result = advertiser->start(info);
    advertiser->stop();
    return result;
}

TEST_CASE("WindowsAdvertiser can start and stop", "[advertiser][windows]") {
    if (!is_mdns_functional()) {
        SKIP("mDNS service not available in this environment");
    }

    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Beebium Test";
    info.port = 48875;
    info.txt_records["uuid"] = "test-uuid-1234";
    info.txt_records["model"] = "model-b";

    SECTION("Start advertising") {
        bool result = advertiser->start(info);
        REQUIRE(result);

        // Wait for async registration callback
        REQUIRE(wait_for_advertising_windows(*advertiser));

        auto state = advertiser->state();
        REQUIRE(state.advertising);
        REQUIRE_FALSE(state.actual_name.empty());
    }

    SECTION("Stop advertising") {
        advertiser->start(info);
        wait_for_advertising_windows(*advertiser);
        advertiser->stop();

        auto state = advertiser->state();
        REQUIRE_FALSE(state.advertising);
    }

    SECTION("Restart advertising") {
        advertiser->start(info);
        wait_for_advertising_windows(*advertiser);
        advertiser->stop();

        bool result = advertiser->start(info);
        REQUIRE(result);
        REQUIRE(wait_for_advertising_windows(*advertiser));
    }
}

TEST_CASE("WindowsAdvertiser handles empty TXT records", "[advertiser][windows]") {
    if (!is_mdns_functional()) {
        SKIP("mDNS service not available in this environment");
    }

    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Minimal Service";
    info.port = 12345;
    // No TXT records

    bool result = advertiser->start(info);
    REQUIRE(result);
    REQUIRE(wait_for_advertising_windows(*advertiser));

    advertiser->stop();
}
#endif
