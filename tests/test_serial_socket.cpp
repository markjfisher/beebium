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

#include <beebium/ModelBHardware.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialConcepts.hpp>
#include <beebium/serial/SerialDevice.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

using namespace beebium;

namespace {

constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_16
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

// Phase 2 / Correction 1, Axis A: serial-socket presence is gated by detecting
// the serial_socket member, so a hardware variant that does not fit the socket
// (a future Master Compact without serial) is correctly excluded -- the rest of
// the stack guards on this concept. Cassette is a separate axis, deliberately
// NOT modelled here.
struct NoSerialHardware {};
static_assert(HasSerialSocket<ModelBHardware>,
              "Model B fits the on-board serial socket");
static_assert(!HasSerialSocket<NoSerialHardware>,
              "hardware without serial_socket is excluded by the concept");

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

// =============================================================================
// Scriptable endpoint end-to-end through the socket
// =============================================================================

TEST_CASE("SerialSocket scriptable endpoint transmits to the client", "[serial][socket]") {
    SerialSocket socket;
    auto endpoint = std::make_shared<ScriptableSerialEndpoint>();
    socket.set_device(endpoint.get());

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // 19200, carrier present
    socket.write_acia(1, 0x5A);                      // write TDR

    for (int i = 0; i < 4000 && endpoint->tx_pending() == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    auto drained = endpoint->drain();
    REQUIRE(drained.size() == 1);
    CHECK(drained[0] == 0x5A);
}

TEST_CASE("SerialSocket scriptable endpoint delivers injected bytes to the Beeb", "[serial][socket]") {
    SerialSocket socket;
    auto endpoint = std::make_shared<ScriptableSerialEndpoint>();
    socket.set_device(endpoint.get());

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    uint8_t payload = 0x39;
    endpoint->inject(&payload, 1);

    for (int i = 0; i < 6000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0x39);
}

// A single SerialPortDevice wired via set_device() serves both directions: a
// loopback device echoes a transmitted byte back so the Beeb receives it.
TEST_CASE("SerialSocket set_device loopback round-trips a byte", "[serial][socket]") {
    SerialSocket socket;
    LoopbackSerialEndpoint endpoint;
    socket.set_device(&endpoint);  // non-owning; endpoint outlives the socket use

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // 19200, carrier present
    socket.write_acia(1, 0xC3);                      // transmit

    for (int i = 0; i < 20000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0xC3);
}

// =============================================================================
// SerialPort handle (the UserPort analogue)
// =============================================================================

TEST_CASE("SerialPort handle attaches a single device and round-trips", "[serial][port]") {
    SerialSocket socket;
    SerialPort port(socket);
    CHECK_FALSE(port.is_occupied());

    LoopbackSerialEndpoint endpoint;
    port.attach(endpoint);
    CHECK(port.is_occupied());

    // The serial port has a single connector: a second attach throws.
    LoopbackSerialEndpoint other;
    CHECK_THROWS(port.attach(other));

    // The attached device drives the round-trip through the socket/ULA.
    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    socket.write_acia(1, 0xC3);
    for (int i = 0; i < 20000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0xC3);

    port.detach();
    CHECK_FALSE(port.is_occupied());
}
