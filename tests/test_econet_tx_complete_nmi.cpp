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

// test_econet_tx_complete_nmi.cpp
//
// Tests for the MC6854 ADLC's TX_LAST_DATA -> frame complete -> IRQ
// sequence, modelling the ANFS ROM's transmit path.
//
// The ANFS ROM's tx_begin writes:
//   CR2 = &E7 (RTS | CLR_TX_ST | CLR_RX_ST | FC_TDRA | FLAG_IDLE | 2_1_BYTE | PSE)
//   CR1 = &44 (RX_RESET | TIE)
// Then NMI-driven writes send scout bytes to the TX FIFO. After the
// last byte, the ROM writes:
//   CR2 = &3F (TX_LAST_DATA | CLR_RX_ST | FLAG_IDLE | FC_TDRA | 2_1_BYTE | PSE)
// which triggers mark_tx_last_and_flush(). After the frame is sent,
// the ADLC must fire an IRQ so nmi_tx_complete can run and write
// CR1 = &82 (TX_RESET | RIE) to clear RX_RESET for scout ack reception.
//
// The bug under investigation: after CR2 = &3F, the stored CR2
// becomes &07 (auto-clearing bits removed), which clears RTS (bit 7).
// CTS goes high. TDRA requires CTS low, so TDRA is not set and no
// IRQ fires. nmi_tx_complete never runs.

#include <catch2/catch_test_macros.hpp>

#include <beebium/econet/EconetSocket.hpp>
#include <beebium/econet/Mc6854.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/TestBackend.hpp>

using namespace beebium;

namespace {

void tick_adlc(Mc6854& adlc, int cycles) {
    for (int i = 0; i < cycles; ++i) {
        adlc.tick_rising();
        adlc.tick_falling();
    }
}

void tick_socket(EconetSocket& socket, int cycles) {
    for (int i = 0; i < cycles; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
}

constexpr uint16_t REG_CR1 = 0;
constexpr uint16_t REG_CR2 = 1;
constexpr uint16_t REG_TX_DATA = 2;
constexpr uint16_t REG_TX_LAST = 3;  // Write with is_last=true (or CR4 if AC set)

} // namespace


TEST_CASE("ADLC IRQ fires after TX_LAST_DATA so NFS ROM can switch to RX mode", "[econet][adlc][tx]") {
    // This test models the critical ANFS ROM sequence:
    // 1. Configure ADLC for TX (RTS asserted, RX_RESET set, TIE enabled)
    // 2. Write a short frame to the TX FIFO
    // 3. Signal frame-complete via CR2 TX_LAST_DATA
    // 4. Verify IRQ fires (allowing nmi_tx_complete to clear RX_RESET)

    TestBackend backend;
    backend.set_connected(true);
    Mc6854 adlc(backend);
    adlc.hard_reset();

    // Configure ADLC for TX (ANFS tx_begin sequence)
    adlc.write(REG_CR2, 0xE7);  // RTS | CLR_TX_ST | CLR_RX_ST | FC_TDRA | FLAG_IDLE | 2_1_BYTE | PSE
    adlc.write(REG_CR1, 0x44);  // RX_RESET | TIE
    tick_adlc(adlc, 10);

    // Verify TDRA is available before writing
    uint8_t sr1_before = adlc.read(0);
    REQUIRE((sr1_before & Mc6854::SR1_TDRA) != 0);

    // Write a 2-byte frame (fits in 3-entry FIFO without overflow)
    adlc.write(REG_TX_DATA, 0xDD);  // dest station
    adlc.write(REG_TX_DATA, 0x00);  // dest network

    // TX_LAST_DATA: mark frame complete and flush
    adlc.write(REG_CR2, 0x3F);  // TX_LAST_DATA | CLR_RX_ST | FLAG_IDLE | FC_TDRA | 2_1_BYTE | PSE

    // Frame should have been sent
    REQUIRE(backend.sent_frame_count() == 1);
    CHECK(backend.sent_frames()[0].size() == 2);

    // After TX_LAST_DATA, CR2 is stored as &0F (auto-clearing bits removed).
    // &3F & ~(TX_LAST_DATA | CLR_RX_ST) = &3F & ~0x30 = &0F.
    // RTS (bit 7) is NOT in the &3F value, so RTS is cleared.
    CHECK(adlc.cr2() == 0x0F);

    // Tick the ADLC
    tick_adlc(adlc, 200);

    uint8_t sr1_after = adlc.read(0);
    INFO("SR1 after TX_LAST_DATA: 0x" << std::hex << (int)sr1_after);
    INFO("CR1: 0x" << std::hex << (int)adlc.cr1());
    INFO("CR2: 0x" << std::hex << (int)adlc.cr2());
    INFO("CTS input: " << adlc.cts_input());
    INFO("TDRA: " << ((sr1_after & Mc6854::SR1_TDRA) ? "set" : "clear"));
    INFO("IRQ output: " << adlc.irq_output());

    // The IRQ MUST fire so the NFS ROM's nmi_tx_complete handler
    // can write CR1=&82 (clearing RX_RESET for scout ack reception).
    //
    // On real hardware, the ADLC fires a "frame complete" interrupt
    // after serialising the CRC and closing flag. In Beebium, the
    // frame is sent atomically via flush_tx_frame(), so there is no
    // serialisation delay. But the IRQ must still fire.
    //
    // Currently this fails because CR2=&3F clears RTS, CTS goes
    // high, and TDRA (which requires CTS low) is not set.
    CHECK(adlc.irq_output());
}
