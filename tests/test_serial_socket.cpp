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

#include <beebium/ModelBHardware.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace beebium;

namespace {

constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_16
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

}  // namespace

// =============================================================================
// SerialSocket standalone
// =============================================================================

TEST_CASE("SerialSocket reset leaves TDRE ready and IRQ clear", "[serial][socket]") {
    SerialSocket socket;
    socket.write_acia(0, CONTROL_8N1);
    CHECK((socket.read_acia(0) & Mc6850::SR_TDRE) != 0);
    CHECK_FALSE(socket.irq_pending());
}

TEST_CASE("SerialSocket ULA write routes to the ACIA /DCD", "[serial][socket]") {
    SerialSocket socket;
    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    CHECK_FALSE(socket.acia().not_dcd());
}

// =============================================================================
// Memory-map integration through ModelBHardware
// =============================================================================

TEST_CASE("Model B maps the ACIA at &FE08/&FE09", "[serial][socket][modelb]") {
    ModelBHardware hw;

    // Control register write at &FE08, status read back at &FE08.
    hw.write(0xFE08, CONTROL_8N1);
    CHECK((hw.read(0xFE08) & Mc6850::SR_TDRE) != 0);

    // &FE08 is mirrored on the low bit across &FE08-&FE0F, so &FE0A reads the
    // same status register, while odd addresses hit the data register.
    CHECK(hw.read(0xFE0A) == hw.read(0xFE08));
}

TEST_CASE("Model B maps the Serial ULA at &FE10", "[serial][socket][modelb]") {
    ModelBHardware hw;
    hw.write(0xFE08, CONTROL_8N1);
    hw.write(0xFE10, SerialUla::RS423_SELECT);
    CHECK(hw.serial_socket.ula().rs423_selected());
    CHECK_FALSE(hw.serial_socket.acia().not_dcd());
}

TEST_CASE("Model B serial ACIA IRQ reaches the IRQ aggregator", "[serial][socket][modelb]") {
    ModelBHardware hw;

    // No serial interrupt initially.
    CHECK((hw.poll_irq() & 0x10) == 0);  // bit 4 = serial ACIA

    // Enable RX IRQ, present carrier, and clock in a received character at the
    // ACIA level so RDRF + RX IRQ assert.
    Mc6850& acia = hw.serial_socket.acia();
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE);
    acia.set_not_dcd(false);
    acia.update_receive(1);
    acia.update_receive(0);
    for (int i = 0; i < 8; ++i) acia.update_receive((0x5A >> i) & 1);
    acia.update_receive(1);

    REQUIRE(acia.irq_pending());
    CHECK((hw.poll_irq() & 0x10) != 0);
}
