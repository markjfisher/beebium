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

// Smoke tests for the discovery::Browser API. End-to-end real-mDNS
// round-trip coverage lives in test_aun_mdns_e2e (Step 4); these
// cases focus on the API surface (state, lifecycle, callback shape).

#include <catch2/catch_test_macros.hpp>

#include <beebium/discovery/Advertiser.hpp>
#include <beebium/discovery/Browser.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace beebium::discovery;

TEST_CASE("create_browser returns non-null", "[browser]") {
    auto browser = create_browser();
    REQUIRE(browser != nullptr);
}

TEST_CASE("Browser initial state", "[browser]") {
    auto browser = create_browser();
    auto s = browser->state();
    CHECK_FALSE(s.browsing);
}

TEST_CASE("Browser stop without start is safe", "[browser]") {
    auto browser = create_browser();
    browser->stop();
    CHECK_FALSE(browser->state().browsing);
}

TEST_CASE("Browser start when unavailable returns false", "[browser]") {
    auto browser = create_browser();
    if (browser->state().available) {
        SUCCEED("mDNS is available; this case only exercises the null path");
        return;
    }
    BrowserCallbacks cbs;
    bool result = browser->start("_aun._udp", cbs);
    CHECK_FALSE(result);
    CHECK_FALSE(browser->state().browsing);
}

#ifdef __APPLE__
TEST_CASE("Bonjour browser is available on macOS", "[browser][macos]") {
    auto browser = create_browser();
    REQUIRE(browser->state().available);
}

TEST_CASE("Bonjour browser sees an advertised service",
          "[browser][macos][integration]") {
    auto advertiser = create_advertiser();
    REQUIRE(advertiser->state().available);

    // Advertise a unique service type per test run so we don't pick
    // up announcements from other test processes or stray daemons.
    // _aun._udp is the type we'll consume in production; piggyback on
    // it because the OS resolver caches per service type.
    ServiceInfo info;
    info.service_type = "_aun._udp";
    info.instance_name = "BrowserTest "
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    info.port = 32999;
    info.txt_records["version"] = "1";
    info.txt_records["net"] = "0";
    info.txt_records["station"] = "99";
    info.txt_records["port"] = "32999";

    REQUIRE(advertiser->start(info));

    // Wait for advertise to land before we start browsing.
    for (int i = 0; i < 50; ++i) {
        if (advertiser->state().advertising) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(advertiser->state().advertising);

    auto browser = create_browser();
    std::atomic<bool> saw_added{false};
    std::atomic<int> add_count{0};
    BrowserCallbacks cbs;
    cbs.on_added = [&](const DiscoveredService& svc) {
        // Filter to the exact instance we just advertised so we don't
        // race on stray daemons.
        if (svc.instance_name.find("BrowserTest ") == 0
                && svc.txt_records.count("net") == 1
                && svc.txt_records.at("station") == "99"
                && svc.port == 32999) {
            saw_added.store(true);
            add_count.fetch_add(1);
        }
    };
    REQUIRE(browser->start("_aun._udp", cbs));

    // Generous timeout: real mDNS resolve + addrinfo can take
    // hundreds of ms even on loopback.
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !saw_added.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(saw_added.load());

    browser->stop();
    advertiser->stop();
}
#endif
