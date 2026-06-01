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

#include <beebium/devices/Mc6850.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace beebium;

namespace {

// 8N1, /16 counter divide, /RTS low, no TX IRQ. This is the control byte the
// BBC MOS programs for the serial port (and what fn-rom relies upon).
constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_16
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);  // word select 0b101 = 8N1

// Drive update_transmit() until a whole character has been serialised, LSB
// first, returning the assembled data byte. Carrier/idle handling is the
// caller's concern.
uint8_t transmit_byte(Mc6850& acia) {
    uint8_t assembled = 0;
    int bit_index = 0;
    bool seen_start = false;
    for (int i = 0; i < 32; ++i) {
        Mc6850::TransmitResult r = acia.update_transmit();
        if (r.type == Mc6850::BitType::Start) {
            seen_start = true;
            bit_index = 0;
            assembled = 0;
        } else if (r.type == Mc6850::BitType::Data) {
            if (r.bit) assembled |= static_cast<uint8_t>(1u << bit_index);
            ++bit_index;
        } else if (r.type == Mc6850::BitType::Stop) {
            break;
        }
    }
    REQUIRE(seen_start);
    return assembled;
}

// Clock a byte into the receiver: idle -> start(0) -> 8 data bits LSB first ->
// stop(1). Assumes carrier is present (/DCD low).
void receive_byte(Mc6850& acia, uint8_t value, uint8_t stop_bit = 1) {
    acia.update_receive(1);          // idle high: moves Idle -> StartBit
    acia.update_receive(0);          // start bit
    for (int i = 0; i < 8; ++i) {
        acia.update_receive((value >> i) & 1);
    }
    acia.update_receive(stop_bit);   // stop bit completes the character
}

}  // namespace

// =============================================================================
// Reset / power-on state
// =============================================================================

TEST_CASE("ACIA power-on: TDRE set, RDRF clear", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
    CHECK((acia.status() & Mc6850::SR_RDRF) == 0);
    CHECK_FALSE(acia.irq_pending());
}

TEST_CASE("ACIA master reset clears status and suppresses IRQ/TDRE", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    // Master reset (counter divide = 11).
    acia.write_control(Mc6850::COUNTER_DIVIDE_MASTER_RESET);
    // While in master reset, TDRE and IRQ are forced low.
    CHECK((acia.status() & Mc6850::SR_TDRE) == 0);
    CHECK((acia.status() & Mc6850::SR_IRQ) == 0);
    CHECK_FALSE(acia.irq_pending());
}

// =============================================================================
// Control register decode
// =============================================================================

TEST_CASE("ACIA decodes 8N1 word format", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    CHECK(acia.word_select() == 0x05);
    CHECK(acia.eight_bit());
    CHECK(acia.parity() == Mc6850::Parity::None);
    CHECK(acia.one_stop_bit());
}

TEST_CASE("ACIA /RTS reflects transmitter control field", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);  // tx control 00 -> /RTS low
    CHECK_FALSE(acia.get_not_rts());
    acia.write_control(CONTROL_8N1
        | (Mc6850::TX_CTRL_RTS_HIGH_NO_IRQ << Mc6850::CR_TX_CONTROL_SHIFT));
    CHECK(acia.get_not_rts());
}

// =============================================================================
// Transmit path (bit-level)
// =============================================================================

TEST_CASE("ACIA transmits a byte LSB first", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    acia.write_data(0x41);
    CHECK((acia.status() & Mc6850::SR_TDRE) == 0);  // TDRE cleared by loading TDR

    CHECK(transmit_byte(acia) == 0x41);
    // After the byte is latched into the shift register, TDRE comes back.
    CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
}

TEST_CASE("ACIA TX IRQ asserted only when transmit interrupt enabled", "[serial][mc6850]") {
    Mc6850 acia;
    // Transmitter control 01 = /RTS low, TX IRQ enabled.
    acia.write_control(CONTROL_8N1
        | (Mc6850::TX_CTRL_RTS_LOW_IRQ << Mc6850::CR_TX_CONTROL_SHIFT));

    acia.write_data(0x55);
    acia.update_transmit();  // start bit latches the byte and raises TX IRQ
    CHECK(acia.irq_pending());
    CHECK((acia.status() & Mc6850::SR_IRQ) != 0);
}

// =============================================================================
// Receive path (bit-level)
// =============================================================================

TEST_CASE("ACIA receives a byte and sets RDRF", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(false);  // carrier present

    receive_byte(acia, 0x42);
    CHECK(acia.rdrf());
    CHECK((acia.status() & Mc6850::SR_RDRF) != 0);
    CHECK(acia.read_data() == 0x42);
    CHECK_FALSE(acia.rdrf());  // reading RDR clears RDRF
}

TEST_CASE("ACIA RX IRQ gated by receive interrupt enable", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE);
    acia.set_not_dcd(false);

    receive_byte(acia, 0x42);
    CHECK(acia.irq_pending());
    acia.read_data();
    CHECK_FALSE(acia.irq_pending());  // reading RDR clears the RX IRQ cause
}

TEST_CASE("ACIA flags framing error on bad stop bit", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(false);

    receive_byte(acia, 0x7F, /*stop_bit=*/0);  // stop bit driven low
    CHECK(acia.rdrf());
    CHECK((acia.status() & Mc6850::SR_FE) != 0);
}

TEST_CASE("ACIA flags overrun when RDR not read", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(false);

    receive_byte(acia, 0x11);   // first byte fills RDR
    receive_byte(acia, 0x22);   // second byte arrives before RDR is read

    // The first byte is preserved; overrun surfaces when RDR is read.
    CHECK(acia.read_data() == 0x11);
    CHECK((acia.status() & Mc6850::SR_OVRN) != 0);
}

TEST_CASE("ACIA holds receiver idle while /DCD high", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(true);  // no carrier

    receive_byte(acia, 0x42);
    CHECK_FALSE(acia.rdrf());  // nothing received without carrier
}
