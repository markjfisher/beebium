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

#include "Rfc2217ServerExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>

using namespace beebium;

TEST_CASE("Rfc2217ServerExtension attaches to the serial port", "[serial][rfc2217]") {
    Rfc2217ServerExtension ext;
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");
}

TEST_CASE("Rfc2217ServerExtension rejects port 0", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ServerExtension ext;
    ext.set_config({{"bind", "127.0.0.1"}, {"port", "0"}});
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Rfc2217ServerExtension requires a port", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ServerExtension ext;
    ext.set_config({{"bind", "127.0.0.1"}});  // no port
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}

TEST_CASE("Rfc2217ServerExtension rejects an unbindable address", "[serial][rfc2217]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    Rfc2217ServerExtension ext;
    ext.set_config({{"bind", "192.0.2.1"}, {"port", "4001"}});  // TEST-NET, unbindable
    CHECK_THROWS_AS(ext.init(ctx), std::runtime_error);
}
