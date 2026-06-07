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

#include "Rfc2217ClientExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>

using namespace beebium;

TEST_CASE("Rfc2217ClientExtension attaches via url=", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");

    ext.set_config({{"url", "rfc2217://127.0.0.1:1"}});
    ext.init(ctx);
    CHECK(port.is_occupied());
    ext.shutdown();
    CHECK_FALSE(port.is_occupied());
}

TEST_CASE("Rfc2217ClientExtension attaches via host=/port=", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    ext.set_config({{"host", "127.0.0.1"}, {"port", "1"}, {"baud", "9600"}});
    ext.init(ctx);
    CHECK(port.is_occupied());
    ext.shutdown();
}

TEST_CASE("Rfc2217ClientExtension rejects url= combined with host=", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    ext.set_config({{"url", "127.0.0.1:1"}, {"host", "elsewhere"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Rfc2217ClientExtension requires an endpoint", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    ext.set_config({{"baud", "9600"}});  // no url / host+port
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Rfc2217ClientExtension accepts framing / flow / dtr", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    ext.set_config({{"url", "rfc2217://127.0.0.1:1"},
                    {"framing", "7E1"},
                    {"flow", "rtscts"},
                    {"dtr", "off"}});
    ext.init(ctx);
    CHECK(port.is_occupied());
    ext.shutdown();
}

TEST_CASE("Rfc2217ClientExtension rejects bad framing", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    ext.set_config({{"url", "rfc2217://127.0.0.1:1"}, {"framing", "9Z3"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Rfc2217ClientExtension rejects bad flow", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ClientExtension ext;
    ext.set_config({{"url", "rfc2217://127.0.0.1:1"}, {"flow", "bogus"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}
