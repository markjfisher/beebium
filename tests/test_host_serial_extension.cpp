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

// POSIX-only: exercises the host-serial extension against a real pty.

#include "HostSerialExtension.hpp"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/PtyMaster.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

using namespace beebium;

namespace {
constexpr uint8_t CONTROL_8N1 =
    Mc6850::COUNTER_DIVIDE_16 | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);
}  // namespace

TEST_CASE("HostSerialExtension attaches to the serial port (pty mode)",
          "[serial][host-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "pty"}});
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");

    ext.init(ctx);
    CHECK(port.is_occupied());

    ext.shutdown();
}

TEST_CASE("HostSerialExtension device mode requires a path",
          "[serial][host-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "device"}});  // no path
    CHECK_THROWS(ext.init(ctx));
    CHECK_FALSE(port.is_occupied());
}

TEST_CASE("HostSerialExtension bridges a transmitted byte to the host device",
          "[serial][host-serial]") {
    // Create a pty; the test holds the master, the extension opens the slave as
    // a device. Bytes the Beeb transmits should appear on the master end.
    auto pty = std::make_unique<serial::PtyMaster>();
    REQUIRE(pty->is_open());
    const std::string slave = pty->slave_path();

    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "device"}, {"path", slave}});
    ext.init(ctx);
    REQUIRE(port.is_occupied());

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // RS423, 19200, carrier
    socket.write_acia(1, 0x5A);                    // transmit 'Z'

    bool got = false;
    uint8_t value = 0;
    for (int attempt = 0; attempt < 20 && !got; ++attempt) {
        for (int i = 0; i < 600; ++i) {
            socket.tick_rising();
            socket.tick_falling();
        }
        std::array<std::uint8_t, 16> buf{};
        serial::ReadResult r = pty->read(std::span<std::uint8_t>(buf.data(), buf.size()));
        if (r.bytes > 0) {
            value = buf[0];
            got = true;
        }
    }
    REQUIRE(got);
    CHECK(value == 0x5A);

    ext.shutdown();
}
