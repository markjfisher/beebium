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

#include <beebium/net/EndpointUrl.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace beebium::net;

TEST_CASE("EndpointUrl parses bare host:port", "[net][url]") {
    std::string error;
    auto e = parse_endpoint_url("localhost:25232", error);
    REQUIRE(e.has_value());
    CHECK(e->scheme.empty());
    CHECK(e->host == "localhost");
    CHECK(e->port == 25232);
}

TEST_CASE("EndpointUrl parses a scheme prefix", "[net][url]") {
    std::string error;
    auto e = parse_endpoint_url("ip232://bbs.example.com:25232", error);
    REQUIRE(e.has_value());
    CHECK(e->scheme == "ip232");
    CHECK(e->host == "bbs.example.com");
    CHECK(e->port == 25232);
}

TEST_CASE("EndpointUrl parses the rfc2217 scheme", "[net][url]") {
    std::string error;
    auto e = parse_endpoint_url("rfc2217://ser2net.local:4001", error);
    REQUIRE(e.has_value());
    CHECK(e->scheme == "rfc2217");
    CHECK(e->host == "ser2net.local");
    CHECK(e->port == 4001);
}

TEST_CASE("EndpointUrl parses a bracketed IPv6 literal", "[net][url]") {
    std::string error;
    auto e = parse_endpoint_url("[::1]:4001", error);
    REQUIRE(e.has_value());
    CHECK(e->host == "::1");
    CHECK(e->port == 4001);

    auto e2 = parse_endpoint_url("tcp://[2001:db8::1]:80", error);
    REQUIRE(e2.has_value());
    CHECK(e2->scheme == "tcp");
    CHECK(e2->host == "2001:db8::1");
    CHECK(e2->port == 80);
}

TEST_CASE("EndpointUrl rejects a missing port", "[net][url]") {
    std::string error;
    CHECK_FALSE(parse_endpoint_url("localhost", error).has_value());
    CHECK(error.find("port") != std::string::npos);
}

TEST_CASE("EndpointUrl rejects a non-numeric or out-of-range port", "[net][url]") {
    std::string error;
    CHECK_FALSE(parse_endpoint_url("host:abc", error).has_value());
    CHECK_FALSE(parse_endpoint_url("host:0", error).has_value());
    CHECK_FALSE(parse_endpoint_url("host:99999", error).has_value());
}

TEST_CASE("EndpointUrl rejects empty and malformed input", "[net][url]") {
    std::string error;
    CHECK_FALSE(parse_endpoint_url("", error).has_value());
    CHECK_FALSE(parse_endpoint_url("[::1:4001", error).has_value());  // unterminated [
    CHECK_FALSE(parse_endpoint_url("://host:1", error).has_value());  // empty scheme
}
