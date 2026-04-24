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

// End-to-end test: two AunBackends, each with an announcer and a
// subscriber, no manual --aun map=. Confirms that mDNS discovery
// auto-populates each backend's peer table and that real AUN traffic
// then routes between them.
//
// This test exercises the full real-mDNS stack and depends on a
// working platform responder (Bonjour on macOS, dnsapi on Windows).
// It is tagged [.mdns] so an operator can skip it in environments
// without mDNS via Catch2's --skip "[.mdns]".

#include <catch2/catch_test_macros.hpp>

#include "AunDiscoveryAnnouncer.hpp"
#include "AunDiscoverySubscriber.hpp"

#include <beebium/discovery/Advertiser.hpp>
#include <beebium/discovery/Browser.hpp>
#include <beebium/econet/AunBackend.hpp>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <chrono>
#include <random>
#include <string>
#include <thread>

using namespace beebium;
using namespace beebium::discovery;
using namespace std::chrono_literals;

namespace {

bool platform_supports_mdns() {
    auto adv = create_advertiser();
    auto br = create_browser();
    return adv->state().available && br->state().available;
}

// Bound the polling so a hung subscription doesn't hang CI forever.
constexpr auto MDNS_TIMEOUT = 8s;
constexpr auto POLL_INTERVAL = 50ms;

// Pick station numbers from a tiny per-run-random set so concurrent
// CI shards don't shadow each other.
std::pair<uint8_t, uint8_t> pick_stations() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(2, 200);
    int a = dist(gen);
    int b;
    do { b = dist(gen); } while (b == a);
    return {static_cast<uint8_t>(a), static_cast<uint8_t>(b)};
}

}  // namespace

TEST_CASE("AUN mDNS e2e: peers discover each other on the same net, no map=",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }

    auto [stn_a, stn_b] = pick_stations();

    AunBackend backend_a(/*local_net=*/0, /*local_stn=*/stn_a, 0);
    AunBackend backend_b(/*local_net=*/0, /*local_stn=*/stn_b, 0);
    REQUIRE(backend_a.is_connected());
    REQUIRE(backend_b.is_connected());

    AunDiscoveryAnnouncer announce_a(0, stn_a, backend_a.local_port(),
                                     "beebium-test", "1.0");
    AunDiscoveryAnnouncer announce_b(0, stn_b, backend_b.local_port(),
                                     "beebium-test", "1.0");
    REQUIRE(announce_a.start());
    REQUIRE(announce_b.start());

    AunDiscoverySubscriber sub_a(backend_a, stn_a);
    AunDiscoverySubscriber sub_b(backend_b, stn_b);
    REQUIRE(sub_a.start());
    REQUIRE(sub_b.start());

    // Wait for both peer tables to learn the other side. We don't
    // require a particular ordering -- whichever gets there first is
    // fine, as long as both make it before the deadline.
    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    bool a_sees_b = false;
    bool b_sees_a = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!a_sees_b) {
            for (const auto& p : backend_a.list_peers()) {
                if (p.net == 0 && p.stn == stn_b
                        && p.port == backend_b.local_port()) {
                    a_sees_b = true;
                    break;
                }
            }
        }
        if (!b_sees_a) {
            for (const auto& p : backend_b.list_peers()) {
                if (p.net == 0 && p.stn == stn_a
                        && p.port == backend_a.local_port()) {
                    b_sees_a = true;
                    break;
                }
            }
        }
        if (a_sees_b && b_sees_a) break;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }

    REQUIRE(a_sees_b);
    REQUIRE(b_sees_a);

    // Discovery must mark them as Discovered, not OperatorConfigured.
    CHECK_FALSE(backend_a.is_operator_configured(0, stn_b));
    CHECK_FALSE(backend_b.is_operator_configured(0, stn_a));

    // Now drive real AUN traffic across the discovered route. The
    // BBC view sets dest_net=0; the backend translates to local_net
    // for lookup -- both sides on net 0 so it's a no-op here.
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.control_byte = 0x42;
    frame.dest_net = 0;
    frame.dest_stn = stn_b;
    frame.src_net = 0;
    frame.src_stn = stn_a;
    frame.data = {0xAA, 0xBB, 0xCC};

    backend_a.send_frame(frame);

    auto frame_deadline = std::chrono::steady_clock::now() + 1s;
    std::optional<NetworkFrame> received;
    while (std::chrono::steady_clock::now() < frame_deadline) {
        received = backend_b.receive_frame();
        if (received.has_value()) break;
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(received.has_value());
    CHECK(received->type == FrameType::Unicast);
    CHECK(received->port == 0x99);
    CHECK(received->control_byte == 0x42);
    CHECK(received->src_stn == stn_a);
    CHECK(received->dest_stn == stn_b);
    REQUIRE(received->data.size() == 3);
    CHECK(received->data[0] == 0xAA);
    CHECK(received->data[1] == 0xBB);
    CHECK(received->data[2] == 0xCC);
}

TEST_CASE("AUN mDNS e2e: cross-net discovery routes via local_net translation",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }

    auto [stn_a, stn_b] = pick_stations();

    // Different absolute nets; both sides should see each other as
    // cross-net peers.
    AunBackend backend_a(/*local_net=*/3, /*local_stn=*/stn_a, 0);
    AunBackend backend_b(/*local_net=*/5, /*local_stn=*/stn_b, 0);
    REQUIRE(backend_a.is_connected());
    REQUIRE(backend_b.is_connected());

    AunDiscoveryAnnouncer announce_a(3, stn_a, backend_a.local_port(),
                                     "beebium-test", "1.0");
    AunDiscoveryAnnouncer announce_b(5, stn_b, backend_b.local_port(),
                                     "beebium-test", "1.0");
    REQUIRE(announce_a.start());
    REQUIRE(announce_b.start());

    AunDiscoverySubscriber sub_a(backend_a, stn_a);
    AunDiscoverySubscriber sub_b(backend_b, stn_b);
    REQUIRE(sub_a.start());
    REQUIRE(sub_b.start());

    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    bool a_sees_b = false, b_sees_a = false;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& p : backend_a.list_peers()) {
            if (p.net == 5 && p.stn == stn_b) { a_sees_b = true; break; }
        }
        for (const auto& p : backend_b.list_peers()) {
            if (p.net == 3 && p.stn == stn_a) { b_sees_a = true; break; }
        }
        if (a_sees_b && b_sees_a) break;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }

    REQUIRE(a_sees_b);
    REQUIRE(b_sees_a);

    // BBC on A explicitly addresses cross-net peer with dest_net=5.
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.dest_net = 5;
    frame.dest_stn = stn_b;
    frame.src_net = 0;
    frame.src_stn = stn_a;
    frame.data = {0xCC};

    backend_a.send_frame(frame);

    auto frame_deadline = std::chrono::steady_clock::now() + 1s;
    std::optional<NetworkFrame> received;
    while (std::chrono::steady_clock::now() < frame_deadline) {
        received = backend_b.receive_frame();
        if (received.has_value()) break;
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(received.has_value());
    // Cross-net: src_net delivered as 3 (A's absolute net), NOT
    // translated to 0 (because A is not on B's local net).
    CHECK(received->src_net == 3);
    CHECK(received->src_stn == stn_a);
    CHECK(received->dest_net == 5);
    CHECK(received->dest_stn == stn_b);
}
