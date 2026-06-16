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

#include "Ip232SerialExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>

using namespace beebium;

namespace {

// A config that connects nowhere useful (127.0.0.1:1 is refused fast); these
// tests exercise attach/config/shutdown, not a live connection. The endpoint's
// connection thread fails the connect off the test thread and shutdown joins it.
std::map<std::string, std::string> base_config() {
    return {{"host", "127.0.0.1"}, {"port", "1"}};
}

}  // namespace

TEST_CASE("Ip232SerialExtension attaches to the serial port", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");
    CHECK(ext.rpc_dispatchers().empty());  // no typed service in v1

    ext.set_config(base_config());
    ext.init(ctx);
    CHECK(port.is_occupied());

    ext.shutdown();
    CHECK_FALSE(port.is_occupied());
}

TEST_CASE("Ip232SerialExtension accepts raw mode", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    auto config = base_config();
    config["mode"] = "raw";
    ext.set_config(config);
    ext.init(ctx);
    CHECK(port.is_occupied());
    ext.shutdown();
}

TEST_CASE("Ip232SerialExtension rejects an unknown mode", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    auto config = base_config();
    config["mode"] = "bogus";
    ext.set_config(config);
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Ip232SerialExtension rejects an out-of-range port", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    ext.set_config({{"host", "127.0.0.1"}, {"port", "70000"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Ip232SerialExtension accepts a url= endpoint", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    // The arg parser strips the quotes; the extension sees the bare value.
    ext.set_config({{"url", "ip232://127.0.0.1:1"}});
    ext.init(ctx);
    CHECK(port.is_occupied());
    ext.shutdown();
}

TEST_CASE("Ip232SerialExtension accepts a bare host:port url=", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    ext.set_config({{"url", "127.0.0.1:1"}});
    ext.init(ctx);
    CHECK(port.is_occupied());
    ext.shutdown();
}

TEST_CASE("Ip232SerialExtension rejects url= combined with host=", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    ext.set_config({{"url", "127.0.0.1:1"}, {"host", "elsewhere"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Ip232SerialExtension rejects an unparseable url=", "[serial][ip232]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Ip232SerialExtension ext;
    ext.set_config({{"url", "no-port-here"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}
