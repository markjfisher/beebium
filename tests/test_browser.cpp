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

    // Advertise on a per-run-unique service type so we don't pick up
    // announcements from other test processes, stray daemons, or stale
    // _aun._udp records left by prior runs -- the browser only ever sees
    // this run's advertisement.
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string type = "_bbt"
        + std::to_string(static_cast<unsigned long long>(stamp) % 1000000ULL)
        + "._udp";
    ServiceInfo info;
    info.service_type = type;
    info.instance_name = "BrowserTest " + std::to_string(stamp);
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
    REQUIRE(browser->start(type, cbs));

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

// Controlled analogue of the in-process two-peer scenario, but on a
// per-run-unique service type so it is fully isolated from any stray
// _aun._udp records (other Beebiums, prior crashed runs). Two advertisers
// and two browsers in one process must cross-discover each other.
TEST_CASE("Two advertisers and two browsers cross-discover in one process",
          "[browser][macos][integration]") {
    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string type = "_bbt" + std::to_string(t % 1000000ULL) + "._udp";

    auto make_info = [&](const std::string& tag, uint16_t port) {
        ServiceInfo info;
        info.service_type = type;
        info.instance_name = "Peer-" + tag + "-"
            + std::to_string(t);
        info.port = port;
        info.txt_records["tag"] = tag;
        return info;
    };

    auto adv_a = create_advertiser();
    auto adv_b = create_advertiser();
    REQUIRE(adv_a->start(make_info("A", 40001)));
    REQUIRE(adv_b->start(make_info("B", 40002)));

    auto saw_tag = [](std::atomic<bool>& flag, const std::string& want) {
        BrowserCallbacks cbs;
        cbs.on_added = [&flag, want](const DiscoveredService& svc) {
            auto it = svc.txt_records.find("tag");
            if (it != svc.txt_records.end() && it->second == want
                    && svc.port != 0) {
                flag.store(true);
            }
        };
        return cbs;
    };

    std::atomic<bool> a_sees_b{false}, b_sees_a{false};
    auto br_a = create_browser();
    auto br_b = create_browser();
    REQUIRE(br_a->start(type, saw_tag(a_sees_b, "B")));
    REQUIRE(br_b->start(type, saw_tag(b_sees_a, "A")));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline
           && !(a_sees_b.load() && b_sees_a.load())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(a_sees_b.load());
    CHECK(b_sees_a.load());

    br_a->stop();
    br_b->stop();
    adv_a->stop();
    adv_b->stop();
}
#endif
