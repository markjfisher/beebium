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

// Tests for AunEconetTransportExtension. The parameter parsers
// (parse_port, parse_map) are pure functions and tested directly.
// create_backend() is exercised through the public extension config
// surface; it constructs a real AunBackend bound to an OS-chosen port.

#include <catch2/catch_test_macros.hpp>

#include <beebium/econet/AunBackend.hpp>
#include "AunEconetTransportExtension.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

using namespace beebium;

TEST_CASE("AunEconetTransportExtension::parse_port: empty -> default",
          "[econet][aun][extension]") {
    auto p = AunEconetTransportExtension::parse_port("");
    REQUIRE(p.has_value());
    REQUIRE(*p == AUN_DEFAULT_PORT);
}

TEST_CASE("AunEconetTransportExtension::parse_port: 'none' -> nullopt",
          "[econet][aun][extension]") {
    auto p = AunEconetTransportExtension::parse_port("none");
    REQUIRE_FALSE(p.has_value());
}

TEST_CASE("AunEconetTransportExtension::parse_port: numeric value",
          "[econet][aun][extension]") {
    auto p = AunEconetTransportExtension::parse_port("32768");
    REQUIRE(p.has_value());
    REQUIRE(*p == 32768);
}

TEST_CASE("AunEconetTransportExtension::parse_port: invalid falls back to default",
          "[econet][aun][extension]") {
    auto p = AunEconetTransportExtension::parse_port("not-a-number");
    REQUIRE(p.has_value());
    REQUIRE(*p == AUN_DEFAULT_PORT);
}

TEST_CASE("AunEconetTransportExtension::parse_map: empty -> empty list",
          "[econet][aun][extension]") {
    std::vector<std::string> entries;
    auto peers = AunEconetTransportExtension::parse_map(entries);
    REQUIRE(peers.empty());
}

TEST_CASE("AunEconetTransportExtension::parse_map: single net.stn entry",
          "[econet][aun][extension]") {
    std::vector<std::string> entries = {"0.254@127.0.0.1@32768"};
    auto peers = AunEconetTransportExtension::parse_map(entries);
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].net == 0);
    CHECK(peers[0].stn == 254);
    CHECK(peers[0].ip_addr_net_byte_order == htonl(INADDR_LOOPBACK));
    CHECK(peers[0].port == 32768);
}

TEST_CASE("AunEconetTransportExtension::parse_map: bare station defaults net to 0",
          "[econet][aun][extension]") {
    std::vector<std::string> entries = {"254@127.0.0.1@32768"};
    auto peers = AunEconetTransportExtension::parse_map(entries);
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].net == 0);
    CHECK(peers[0].stn == 254);
}

TEST_CASE("AunEconetTransportExtension::parse_map: multiple entries",
          "[econet][aun][extension]") {
    std::vector<std::string> entries = {
        "0.254@127.0.0.1@32768",
        "0.253@127.0.0.1@32769",
    };
    auto peers = AunEconetTransportExtension::parse_map(entries);
    REQUIRE(peers.size() == 2);
    CHECK(peers[0].stn == 254);
    CHECK(peers[0].port == 32768);
    CHECK(peers[1].stn == 253);
    CHECK(peers[1].port == 32769);
}

TEST_CASE("AunEconetTransportExtension::parse_map: malformed entries are skipped",
          "[econet][aun][extension]") {
    std::vector<std::string> entries = {
        "broken@entry",                // only 2 fields -> dropped
        "0.254@127.0.0.1@32768",
    };
    auto peers = AunEconetTransportExtension::parse_map(entries);
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].stn == 254);
}

TEST_CASE("AunEconetTransportExtension::create_backend: port=none returns nullptr",
          "[econet][aun][extension]") {
    AunEconetTransportExtension ext;
    ext.set_config({{"port", "none"}});
    auto backend = ext.create_backend(/*station=*/32);
    REQUIRE(backend == nullptr);
}

TEST_CASE("AunEconetTransportExtension::create_backend: port=0 binds an ephemeral port",
          "[econet][aun][extension]") {
    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}});  // OS-chosen
    auto backend = ext.create_backend(/*station=*/32);
    REQUIRE(backend != nullptr);
    REQUIRE(backend->is_connected());
    auto* aun = dynamic_cast<AunBackend*>(backend.get());
    REQUIRE(aun != nullptr);
    REQUIRE(aun->local_port() != 0);
}

TEST_CASE("AunEconetTransportExtension::create_backend: applies map peers",
          "[econet][aun][extension]") {
    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}});
    ext.set_list_config({{"map", {"0.254@127.0.0.1@32768",
                                  "0.253@127.0.0.1@32769"}}});
    auto backend = ext.create_backend(/*station=*/32);
    REQUIRE(backend != nullptr);
    auto* aun = dynamic_cast<AunBackend*>(backend.get());
    REQUIRE(aun != nullptr);
    REQUIRE(aun->peer_count() == 2);
}
