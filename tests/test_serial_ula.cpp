// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "SerialTestDevice.hpp"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace beebium;

namespace {

constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_16
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

// RS423 selected, 19200 baud on both TX and RX (baud-select index 0).
constexpr uint8_t ULA_RS423_19200 = SerialUla::RS423_SELECT | 0x00;

}  // namespace

// =============================================================================
// Control-latch decode
// =============================================================================

TEST_CASE("SerialUla decodes baud-rate selects", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);

    ula.write(0, 0x00);  // tx=0 (19200), rx=0 (19200)
    CHECK(ula.tx_baud() == 19200);
    CHECK(ula.rx_baud() == 19200);

    // tx select = 4 (9600), rx select = 1 (1200)
    ula.write(0, (4 << 0) | (1 << SerialUla::RX_BAUD_SHIFT));
    CHECK(ula.tx_baud() == 9600);
    CHECK(ula.rx_baud() == 1200);
}

TEST_CASE("SerialUla RS423 select drives ACIA /DCD", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);

    ula.write(0, 0x00);                       // cassette select
    CHECK(acia.not_dcd());                     // /DCD high -> no carrier
    ula.write(0, SerialUla::RS423_SELECT);     // RS423 select
    CHECK_FALSE(acia.not_dcd());               // /DCD low -> carrier present
}

TEST_CASE("SerialUla decodes cassette motor bit", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    CHECK_FALSE(ula.motor_on());
    ula.write(0, SerialUla::MOTOR_ENABLE);
    CHECK(ula.motor_on());
}

TEST_CASE("SerialUla bit period derives from baud rate", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    ula.write(0, 0x00);  // 19200 baud
    // CPU_HZ / 19200 == 104 ticks per bit.
    CHECK(ula.tx_bit_period() == 104);
    CHECK(ula.rx_bit_period() == 104);
}

// =============================================================================
// Byte <-> bit shifting through the ACIA
// =============================================================================

TEST_CASE("SerialUla shifts a transmitted byte to the sink", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_sink(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);
    acia.write_data(0x53);  // 'S'

    // Clock well past one character time (10 bits * 104 ticks/bit).
    for (int i = 0; i < 4000 && device.sent().empty(); ++i) {
        ula.tick();
    }

    REQUIRE_FALSE(device.sent().empty());
    CHECK(device.sent()[0] == 0x53);
}

TEST_CASE("SerialUla shifts a received byte into the ACIA", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);   // carrier present
    device.push_from_device(0x6A);

    for (int i = 0; i < 6000 && !acia.rdrf(); ++i) {
        ula.tick();
    }

    REQUIRE(acia.rdrf());
    CHECK(acia.read_data() == 0x6A);
}

TEST_CASE("SerialUla full loopback echoes transmitted bytes back", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device(/*echo=*/true);
    ula.set_sink(&device);
    ula.set_source(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);
    acia.write_data(0xC3);

    for (int i = 0; i < 20000 && !acia.rdrf(); ++i) {
        ula.tick();
    }

    REQUIRE(acia.rdrf());
    CHECK(acia.read_data() == 0xC3);
}
