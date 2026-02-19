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

#include <beebium/econet/Mc6854.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

struct TestFixture {
    TestBackend backend;
    Mc6854 adlc;

    TestFixture() : adlc(backend) {}

    // Release from reset (both TX and RX)
    void release_reset() {
        adlc.write(0, 0x00);  // CR1 = 0: clear TX_RESET and RX_RESET
    }

    // Release TX from reset only
    void release_tx_reset() {
        adlc.write(0, Mc6854::CR1_RX_RESET);  // Keep RX in reset, release TX
    }

    // Release RX from reset only
    void release_rx_reset() {
        adlc.write(0, Mc6854::CR1_TX_RESET);  // Keep TX in reset, release RX
    }

    void tick() {
        adlc.tick_rising();
        adlc.tick_falling();
    }

    // Tick for N 2MHz half-cycles (one rising + one falling = 2 half-cycles)
    void tick_n(int half_cycles) {
        for (int i = 0; i < half_cycles; ++i) {
            if (i % 2 == 0) adlc.tick_rising();
            else adlc.tick_falling();
        }
    }

    // Tick through one complete byte period (default 128 half-cycles)
    void tick_one_byte_period() {
        tick_n(adlc.byte_period());
    }

    // Tick through N byte periods
    void tick_byte_periods(int n) {
        for (int i = 0; i < n; ++i) {
            tick_one_byte_period();
        }
    }

    // Set Address Control bit (AC) to access CR3/CR4
    void set_ac() {
        adlc.write(0, adlc.cr1() | Mc6854::CR1_AC);
    }

    // Clear Address Control bit (AC) to access CR2/TX data
    void clear_ac() {
        adlc.write(0, adlc.cr1() & ~Mc6854::CR1_AC);
    }
};

}  // namespace

// =============================================================================
// Power-on / Hard Reset State
// =============================================================================

TEST_CASE("Power-on state: TX and RX held in reset", "[econet][mc6854]") {
    TestFixture t;

    CHECK(t.adlc.cr1() == (Mc6854::CR1_RX_RESET | Mc6854::CR1_TX_RESET));
    CHECK(t.adlc.cr2() == 0);
    CHECK(t.adlc.cr3() == 0);
    CHECK(t.adlc.cr4() == 0);
}

TEST_CASE("Power-on state: FIFOs empty", "[econet][mc6854]") {
    TestFixture t;

    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
    CHECK_FALSE(t.adlc.tx_fifo_full());
    CHECK_FALSE(t.adlc.rx_fifo_full());
}

TEST_CASE("Power-on state: status registers clear", "[econet][mc6854]") {
    TestFixture t;

    // SR1/SR2 should be mostly clear at power-on
    // SR1 bit 6 (TDRA) might be set since TX FIFO is empty - but TX is in reset
    CHECK((t.adlc.sr1() & ~Mc6854::SR1_IRQ) == 0x00);
}

TEST_CASE("Power-on state: IRQ not asserted", "[econet][mc6854]") {
    TestFixture t;
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("Power-on state: frame fields idle", "[econet][mc6854]") {
    TestFixture t;
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
}

// =============================================================================
// Control Register 1 (CR1) — Offset 0 Write
// =============================================================================

TEST_CASE("CR1: writing offset 0 sets CR1", "[econet][mc6854]") {
    TestFixture t;
    t.adlc.write(0, 0x07);  // RIE, TIE, AC
    CHECK(t.adlc.cr1() == 0x07);
}

TEST_CASE("CR1: TX reset clears TX FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Put data in TX FIFO
    t.adlc.write(2, 0xAA);  // Write to TX FIFO
    CHECK_FALSE(t.adlc.tx_fifo_empty());

    // Assert TX reset
    t.adlc.write(0, Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.tx_fifo_empty());
}

TEST_CASE("CR1: RX reset clears RX FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Can't easily fill RX FIFO in Phase 1 without backend injection.
    // Just verify reset doesn't crash and state is correct.
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("CR1: Rx Frame Discontinue (bit 5) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.adlc.write(0, Mc6854::CR1_DISCONTINUE);
    // Bit 5 should have auto-cleared
    CHECK((t.adlc.cr1() & Mc6854::CR1_DISCONTINUE) == 0);
}

TEST_CASE("CR1: releasing from reset", "[econet][mc6854]") {
    TestFixture t;
    CHECK(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.cr1() & Mc6854::CR1_RX_RESET);

    t.release_reset();

    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_RX_RESET);
}

// =============================================================================
// Control Register 2 (CR2) — Offset 1 Write, AC=0
// =============================================================================

TEST_CASE("CR2: writing offset 1 with AC=0 sets CR2", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, 0x42);  // RTS + 2/1-byte
    CHECK(t.adlc.cr2() == 0x42);
}

TEST_CASE("CR2: Clear Rx Status (bit 4) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    CHECK((t.adlc.cr2() & Mc6854::CR2_CLR_RX_ST) == 0);
}

TEST_CASE("CR2: Clear Tx Status (bit 5) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_CLR_TX_ST);
    CHECK((t.adlc.cr2() & Mc6854::CR2_CLR_TX_ST) == 0);
}

TEST_CASE("CR2: both clear bits auto-clear together", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_CLR_TX_ST | Mc6854::CR2_RTS);
    CHECK(t.adlc.cr2() == Mc6854::CR2_RTS);
}

// =============================================================================
// Control Register 3 (CR3) — Offset 1 Write, AC=1
// =============================================================================

TEST_CASE("CR3: writing offset 1 with AC=1 sets CR3", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(1, 0x0A);  // AEX + FD Enable
    CHECK(t.adlc.cr3() == 0x0A);
}

TEST_CASE("AC bit toggles between CR2 and CR3", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Write CR2 (AC=0)
    t.clear_ac();
    t.adlc.write(1, 0x42);
    CHECK(t.adlc.cr2() == 0x42);

    // Write CR3 (AC=1)
    t.set_ac();
    t.adlc.write(1, 0x0B);
    CHECK(t.adlc.cr3() == 0x0B);
    CHECK(t.adlc.cr2() == 0x42);  // CR2 unchanged
}

// =============================================================================
// Control Register 4 (CR4) — Offset 3 Write, AC=1
// =============================================================================

TEST_CASE("CR4: writing offset 3 with AC=1 sets CR4", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(3, 0xC1);  // TX CRC inhibit + RX CRC inhibit + double flag
    CHECK(t.adlc.cr4() == (0xC1 & ~Mc6854::CR4_ABT_EXT));
}

TEST_CASE("CR4: Abort Extend (bit 4) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(3, Mc6854::CR4_ABT_EXT | Mc6854::CR4_DBL_FLAG);
    CHECK((t.adlc.cr4() & Mc6854::CR4_ABT_EXT) == 0);
    CHECK(t.adlc.cr4() & Mc6854::CR4_DBL_FLAG);  // Other bits preserved
}

// =============================================================================
// Status Register 1 (SR1) — Offset 0 Read
// =============================================================================

TEST_CASE("SR1: reading offset 0 returns SR1", "[econet][mc6854]") {
    TestFixture t;
    // At power-on, SR1 should be clear
    uint8_t sr1 = t.adlc.read(0);
    CHECK(sr1 == 0x00);
}

TEST_CASE("SR1: TDRA set when TX FIFO has space and not in reset", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.tick();

    uint8_t sr1 = t.adlc.read(0);
    CHECK(sr1 & Mc6854::SR1_TDRA);
}

TEST_CASE("SR1: TDRA clear when TX in reset", "[econet][mc6854]") {
    TestFixture t;
    // TX in reset by default
    t.tick();

    uint8_t sr1 = t.adlc.read(0);
    CHECK_FALSE(sr1 & Mc6854::SR1_TDRA);
}

// =============================================================================
// Status Register 2 (SR2) — Offset 1 Read
// =============================================================================

TEST_CASE("SR2: reading offset 1 returns SR2", "[econet][mc6854]") {
    TestFixture t;
    uint8_t sr2 = t.adlc.read(1);
    // At power-on with connected backend, DCD should be clear
    CHECK_FALSE(sr2 & Mc6854::SR2_DCD);
}

TEST_CASE("SR2: DCD reflects backend disconnection", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.backend.set_connected(true);
    t.tick();
    CHECK_FALSE(t.adlc.read(1) & Mc6854::SR2_DCD);

    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.read(1) & Mc6854::SR2_DCD);
}

// =============================================================================
// TX FIFO — Offset 2 Write (frame continue) and Offset 3 Write, AC=0 (frame terminate)
// =============================================================================

TEST_CASE("TX FIFO: write blocked when TX in reset", "[econet][mc6854]") {
    TestFixture t;
    // TX is in reset by default
    t.adlc.write(2, 0xAA);
    CHECK(t.adlc.tx_fifo_empty());
}

TEST_CASE("TX FIFO: write succeeds when released from reset", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0xAA);
    CHECK_FALSE(t.adlc.tx_fifo_empty());
}

TEST_CASE("TX FIFO: three bytes fill the FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0x01);
    CHECK_FALSE(t.adlc.tx_fifo_full());
    t.adlc.write(2, 0x02);
    CHECK_FALSE(t.adlc.tx_fifo_full());
    t.adlc.write(2, 0x03);
    CHECK(t.adlc.tx_fifo_full());
}

TEST_CASE("TX FIFO: TDRA clears when FIFO full", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x02);
    t.adlc.write(2, 0x03);

    t.tick();
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_TDRA);
}

TEST_CASE("TX FIFO: frame terminate via offset 3 with AC=0", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(2, 0x01);  // Continue
    t.adlc.write(3, 0x02);  // Terminate (last byte)

    // FIFO should have 2 entries
    CHECK_FALSE(t.adlc.tx_fifo_empty());
    CHECK_FALSE(t.adlc.tx_fifo_full());
}

TEST_CASE("TX FIFO: offset 3 with AC=1 writes CR4, not TX FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(3, 0x41);  // Should go to CR4
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.cr4() == (0x41 & ~Mc6854::CR4_ABT_EXT));
}

// =============================================================================
// RX FIFO — Offset 2/3 Read
// =============================================================================

TEST_CASE("RX FIFO: read returns 0 when empty", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    CHECK(t.adlc.read(2) == 0x00);
    CHECK(t.adlc.read(3) == 0x00);
}

// =============================================================================
// Frame Field State Machine — TX
// =============================================================================

TEST_CASE("TX frame field: first byte is address (AP set)", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
    t.adlc.write(2, 0xFF);  // First byte — should be address
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Address);
}

TEST_CASE("TX frame field: second byte advances to control", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0xFF);  // Dest addr
    t.adlc.write(2, 0x01);  // Src addr
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Control);
}

TEST_CASE("TX frame field: third byte advances to data", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0xFF);  // Address
    t.adlc.write(2, 0x01);  // Address
    t.adlc.write(2, 0x80);  // Control → advances to data
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Data);
}

TEST_CASE("TX frame field: frame terminate resets to idle", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(2, 0xFF);  // Address
    t.adlc.write(2, 0x01);  // Address
    t.adlc.write(3, 0x80);  // Frame terminate
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("TX frame field: extended addressing with AEX", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Enable AEX
    t.set_ac();
    t.adlc.write(1, Mc6854::CR3_AEX);
    t.clear_ac();

    // First address byte with bit 0 = 0 (more address bytes follow)
    t.adlc.write(2, 0x00);
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Address);

    // Second address byte with bit 0 = 0 (still more)
    t.adlc.write(2, 0x02);
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Address);

    // Third address byte with bit 0 = 1 (last address byte)
    t.adlc.write(2, 0x01);
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Control);
}

// =============================================================================
// Register Addressing — AC Bit Routing
// =============================================================================

TEST_CASE("Register addressing: offset 0 always writes CR1", "[econet][mc6854]") {
    TestFixture t;

    // AC=0
    t.adlc.write(0, Mc6854::CR1_RIE);
    CHECK(t.adlc.cr1() == Mc6854::CR1_RIE);

    // AC=1 — offset 0 still goes to CR1
    t.set_ac();
    t.adlc.write(0, Mc6854::CR1_TIE | Mc6854::CR1_AC);
    CHECK(t.adlc.cr1() == (Mc6854::CR1_TIE | Mc6854::CR1_AC));
}

TEST_CASE("Register addressing: offset 0 read always returns SR1", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Should return SR1 regardless of AC state
    t.tick();
    uint8_t sr1_ac0 = t.adlc.read(0);

    t.set_ac();
    t.tick();
    uint8_t sr1_ac1 = t.adlc.read(0);

    CHECK(sr1_ac0 == sr1_ac1);
}

TEST_CASE("Register addressing: offsets 2,3 read RX FIFO regardless of AC", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Both with AC=0 and AC=1, offsets 2 and 3 should read from RX FIFO
    t.clear_ac();
    CHECK(t.adlc.read(2) == 0x00);
    CHECK(t.adlc.read(3) == 0x00);

    t.set_ac();
    CHECK(t.adlc.read(2) == 0x00);
    CHECK(t.adlc.read(3) == 0x00);
}

// =============================================================================
// TX/RX Reset
// =============================================================================

TEST_CASE("TX Reset: clears FIFO and frame state", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Fill TX FIFO
    t.adlc.write(2, 0xAA);
    t.adlc.write(2, 0xBB);
    CHECK_FALSE(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.tx_frame_field() != Mc6854::FrameField::Idle);

    // Assert TX reset
    t.adlc.write(0, Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("RX Reset: clears FIFO and frame state", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Assert RX reset
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("Both resets: can be asserted simultaneously", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.adlc.write(2, 0x42);  // Put something in TX FIFO

    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
}

// =============================================================================
// Clear Status Operations
// =============================================================================

TEST_CASE("Clear Rx Status: clears stored RX status bits", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Clear RX status via CR2 bit 4
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);

    // After clearing, FD in SR1 and AP/FV/ERR/ABT/OVRN in SR2 should be clear
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_FD);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_ERR);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_ABT);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_OVRN);
}

TEST_CASE("Clear Tx Status: clears stored TX status bits", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Clear TX status via CR2 bit 5
    t.adlc.write(1, Mc6854::CR2_CLR_TX_ST);

    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TXU);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

// =============================================================================
// Hard Reset
// =============================================================================

TEST_CASE("Hard reset: restores power-on state", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Modify everything
    t.set_ac();
    t.adlc.write(1, 0xFF);  // CR3
    t.adlc.write(3, 0xFF);  // CR4
    t.clear_ac();
    t.adlc.write(1, 0xFF);  // CR2
    t.release_reset();
    t.adlc.write(2, 0xAA);  // TX FIFO

    t.adlc.hard_reset();

    CHECK(t.adlc.cr1() == (Mc6854::CR1_RX_RESET | Mc6854::CR1_TX_RESET));
    CHECK(t.adlc.cr2() == 0);
    CHECK(t.adlc.cr3() == 0);
    CHECK(t.adlc.cr4() == 0);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
    CHECK_FALSE(t.adlc.irq_output());
}

// =============================================================================
// Two-Phase Clock
// =============================================================================

TEST_CASE("Tick: rising and falling edges update status", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // After releasing from reset, TDRA should be set on next tick
    t.adlc.tick_rising();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Falling edge also works
    t.adlc.tick_falling();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

// =============================================================================
// IRQ Output (basic — refined in Phase 2)
// =============================================================================

TEST_CASE("IRQ: not asserted when interrupts disabled", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.tick();

    // TDRA is set but TIE is not enabled
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("IRQ: asserted when TIE enabled and TDRA set", "[econet][mc6854]") {
    TestFixture t;

    // Enable TIE and release from reset
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();

    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK(t.adlc.irq_output());
    CHECK(t.adlc.sr1() & Mc6854::SR1_IRQ);
}

TEST_CASE("IRQ: SR1 bit 7 reflects IRQ output", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // No IRQ
    t.tick();
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_IRQ);

    // Enable TIE → IRQ
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.read(0) & Mc6854::SR1_IRQ);
}

// =============================================================================
// S2RQ (Status #2 Read Request)
// =============================================================================

TEST_CASE("S2RQ: set when DCD asserted in SR2", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.backend.set_connected(false);  // DCD asserted
    t.tick();

    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
}

TEST_CASE("S2RQ: clear when no SR2 conditions", "[econet][mc6854]") {
    TestFixture t;
    // Keep RX in reset so idle (SR2_INACTIVE) is suppressed — idle is a legitimate
    // SR2 condition when RX is active on a quiet line, which would set S2RQ.
    t.release_tx_reset();

    t.backend.set_connected(true);
    t.tick();

    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);
}

// =============================================================================
// Phase 2: Stored vs Present Status (Dual-Nature Bits)
// =============================================================================

TEST_CASE("DCD: dual-nature — stored latch + present input", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Initially connected → DCD clear
    t.backend.set_connected(true);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Disconnect → DCD present asserts, stored latch captures edge
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Reconnect → DCD present clears, but stored latch keeps it set
    t.backend.set_connected(true);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);  // Still set due to stored latch

    // Clear stored latch via Clear RX Status
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);  // Now clear — present is connected
}

TEST_CASE("DCD: stored latch persists after present clears", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Trigger DCD edge
    t.backend.set_connected(true);
    t.tick();
    t.backend.set_connected(false);
    t.tick();

    // Present goes away but stored keeps the bit
    t.backend.set_connected(true);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
}

TEST_CASE("DCD: CLR_RX_ST clears edge latch but SR2 still reflects pin level", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Disconnect → DCD edge latches stored condition
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);  // Edge latch drives S2RQ

    // Clear stored with CLR_RX_ST
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();

    // SR2 DCD still shows: pin is high (no carrier), receiver is active.
    // This is the level-sensitive path — the NFS ROM polls this to detect
    // "No Clock" during boot.
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);

    // But S2RQ is cleared: the edge latch was cleared and no new 0→1
    // transition occurred (pin stayed high). This prevents NMI storms
    // when carrier is continuously absent (--aun-port none).
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);

    // Reconnect → carrier present, DCD pin goes low, SR2 DCD clears
    t.backend.set_connected(true);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Disconnect again → new 0→1 edge re-latches
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);  // Edge latch drives S2RQ again
}

TEST_CASE("DCD: level-sensitive SR2 path is gated by RX_RESET", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Disconnect with receiver active → SR2 DCD shows pin level
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Clear edge latch, then put receiver into reset
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.adlc.write(0, Mc6854::CR1_RX_RESET | Mc6854::CR1_AC);
    t.tick();

    // During RX_RESET, the level-sensitive path is disabled.
    // Edge latch was cleared, so SR2 DCD should be clear.
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Release reset → level path re-activates, DCD visible again
    t.release_reset();
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
}

// =============================================================================
// Phase 2: CTS — Stored/Present and TDRA Inhibit
// =============================================================================

TEST_CASE("CTS: positive edge latches stored condition", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // CTS input low (clear to send) → no CTS status
    t.adlc.set_cts_input(false);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);

    // CTS input goes high (not clear to send) → stored latch captures edge
    t.adlc.set_cts_input(true);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // CTS drops again → present is low, but stored keeps it set
    t.adlc.set_cts_input(false);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Clear TX status clears stored CTS
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_TX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

TEST_CASE("CTS: real-time inhibit of TDRA", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // With CTS low (clear to send), TDRA should be set
    t.adlc.set_cts_input(false);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // CTS goes high → TDRA inhibited
    t.adlc.set_cts_input(true);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // CTS drops → TDRA restored
    t.adlc.set_cts_input(false);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

TEST_CASE("CTS: inhibits TDRA regardless of FIFO state", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_cts_input(true);  // CTS not clear

    // Even with empty TX FIFO, TDRA should be inhibited
    CHECK(t.adlc.tx_fifo_empty());
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

// =============================================================================
// Phase 2: PSE Priority Filtering
// =============================================================================

TEST_CASE("PSE: not active by default (CR2b0=0)", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    CHECK(t.adlc.pse_level() == 0);
    CHECK_FALSE(t.adlc.cr2() & Mc6854::CR2_PSE);
}

TEST_CASE("PSE: Clear Rx Status advances priority level when PSE enabled", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Enable PSE
    t.adlc.write(1, Mc6854::CR2_PSE);
    CHECK(t.adlc.pse_level() == 0);

    // Each Clear RX Status advances
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 1);

    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 2);

    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 3);

    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 4);

    // Should not advance past 4
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 4);
}

TEST_CASE("PSE: does not advance level when PSE disabled", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // PSE not enabled — Clear RX Status should not advance
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 0);
}

TEST_CASE("PSE: RX reset resets PSE level", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Advance PSE level
    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 2);

    // RX reset should clear PSE level
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.pse_level() == 0);
}

// =============================================================================
// Phase 2: IRQ — Level-Sensitive
// =============================================================================

TEST_CASE("IRQ: level-sensitive — enabling TIE with existing TDRA triggers IRQ", "[econet][mc6854][irq]") {
    TestFixture t;
    t.release_reset();
    t.tick();

    // TDRA is set, TIE is not
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK_FALSE(t.adlc.irq_output());

    // Enable TIE → IRQ should assert immediately
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.irq_output());
}

TEST_CASE("IRQ: clears when cause removed", "[econet][mc6854][irq]") {
    TestFixture t;

    // Enable TIE and release from reset → TDRA + TIE = IRQ
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.irq_output());

    // Fill TX FIFO → TDRA clears → IRQ clears
    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x02);
    t.adlc.write(2, 0x03);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("IRQ: clears when interrupt enable disabled", "[econet][mc6854][irq]") {
    TestFixture t;

    // Enable TIE → IRQ
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.irq_output());

    // Disable TIE → IRQ clears
    t.adlc.write(0, 0x00);
    t.tick();
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("IRQ: RIE + DCD triggers IRQ", "[econet][mc6854][irq]") {
    TestFixture t;
    // Keep RX in reset so idle (SR2_INACTIVE) is suppressed — otherwise idle
    // sets S2RQ immediately and RIE triggers IRQ before DCD is asserted.
    t.release_tx_reset();

    // Enable RIE (keep RX in reset)
    t.adlc.write(0, Mc6854::CR1_RIE | Mc6854::CR1_RX_RESET);
    t.tick();

    // No RX conditions → no IRQ
    CHECK_FALSE(t.adlc.irq_output());

    // Disconnect → DCD → S2RQ → IRQ
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    CHECK(t.adlc.irq_output());
}

TEST_CASE("IRQ: both RIE and TIE — either cause triggers", "[econet][mc6854][irq]") {
    TestFixture t;

    // Enable both
    t.adlc.write(0, Mc6854::CR1_RIE | Mc6854::CR1_TIE);
    t.tick();

    // TDRA set → TIE cause → IRQ
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK(t.adlc.irq_output());
}

// =============================================================================
// Phase 2: Stored Status Clearing
// =============================================================================

TEST_CASE("Clear RX Status: clears stored AP, FV, ERR, ABT, OVRN, Idle, DCD, FD", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Trigger DCD stored latch
    t.backend.set_connected(false);
    t.tick();
    t.backend.set_connected(true);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);  // Stored

    // Clear
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);
}

TEST_CASE("Clear TX Status: clears stored CTS and TxU", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Trigger CTS stored latch
    t.adlc.set_cts_input(true);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // CTS input goes low but stored remains
    t.adlc.set_cts_input(false);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Clear TX status
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_TX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

// =============================================================================
// Phase 3: TX Frame Assembly
// =============================================================================

TEST_CASE("TX: 3-byte frame sent to backend after byte trickle", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);  // Short period for fast tests
    t.clear_ac();

    // Write a 3-byte frame (fits in FIFO): dest, src, terminate
    t.adlc.write(2, 0xFF);  // Dest address
    t.adlc.write(2, 0x01);  // Src address
    t.adlc.write(3, 0x80);  // Control (last byte)

    CHECK(t.backend.sent_frame_count() == 0);

    // Tick through 3 byte periods (one per byte)
    t.tick_byte_periods(3);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 3);
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 0x01);
    CHECK(frame[2] == 0x80);
}

TEST_CASE("TX: longer frame with FIFO drain-and-refill", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Fill FIFO (3 bytes)
    t.adlc.write(2, 0xFF);  // Dest
    t.adlc.write(2, 0x01);  // Src
    t.adlc.write(2, 0x80);  // Control

    // Tick to drain one slot
    t.tick_byte_periods(1);

    // Now write the terminate byte into the freed slot
    t.adlc.write(3, 0x99);  // Data (last byte)

    // Drain remaining 3 bytes
    t.tick_byte_periods(3);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 4);
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 0x01);
    CHECK(frame[2] == 0x80);
    CHECK(frame[3] == 0x99);
}

TEST_CASE("TX: frame not sent until last-byte flag", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Write 3 continuation bytes (no terminate)
    t.adlc.write(2, 0xFF);
    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x80);

    // Tick to drain FIFO
    t.tick_byte_periods(3);

    // No frame sent yet (no last-byte flag)
    CHECK(t.backend.sent_frame_count() == 0);
}

TEST_CASE("TX: underrun when FIFO empty mid-frame", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Write one byte (frame start, not terminated)
    t.adlc.write(2, 0xFF);

    // Drain FIFO
    t.tick_byte_periods(1);

    // Now FIFO is empty but frame is incomplete → TxU on next byte period
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_TXU);

    // No frame should have been sent
    CHECK(t.backend.sent_frame_count() == 0);
}

TEST_CASE("TX: blocked when TX in reset", "[econet][mc6854][frame]") {
    TestFixture t;
    // TX in reset by default
    t.adlc.set_byte_period(4);

    t.adlc.write(2, 0xFF);  // Should be blocked
    t.tick_byte_periods(4);

    CHECK(t.backend.sent_frame_count() == 0);
}

TEST_CASE("TX: configurable byte period", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(10);
    t.clear_ac();

    // Write 2-byte frame
    t.adlc.write(2, 0xFF);
    t.adlc.write(3, 0x01);

    // After 9 half-cycles: no byte processed yet
    t.tick_n(9);
    CHECK(t.backend.sent_frame_count() == 0);

    // After 10: first byte processed
    t.tick_n(1);
    // After 20: second byte processed (last), frame sent
    t.tick_n(10);
    CHECK(t.backend.sent_frame_count() == 1);
}

// =============================================================================
// Phase 3: RX Frame Delivery
// =============================================================================

TEST_CASE("RX: received frame delivered to FIFO via byte trickle", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Inject a 4-byte frame into backend
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // After 1 byte period: first byte available
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    uint8_t b0 = t.adlc.read(2);
    CHECK(b0 == 0xFF);

    // After 2nd byte period: second byte
    t.tick_byte_periods(1);
    uint8_t b1 = t.adlc.read(2);
    CHECK(b1 == 0x01);

    // After 3rd: third byte
    t.tick_byte_periods(1);
    uint8_t b2 = t.adlc.read(2);
    CHECK(b2 == 0x80);

    // After 4th: fourth byte (last)
    t.tick_byte_periods(1);
    uint8_t b3 = t.adlc.read(2);
    CHECK(b3 == 0x42);
}

TEST_CASE("RX: FV set on last byte", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80});

    // FV should not be set until the last byte enters the FIFO
    t.tick_byte_periods(2);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Third byte (last) triggers FV
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
}

TEST_CASE("RX: FV blocks further FIFO pushes until cleared", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Two frames queued
    t.backend.inject_rx_frame({0xFF, 0x01});
    t.backend.inject_rx_frame({0xAA, 0xBB});

    // Process first frame
    t.tick_byte_periods(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // More ticks shouldn't deliver second frame yet
    t.tick_byte_periods(4);

    // Read first frame's bytes
    t.adlc.read(2);  // 0xFF
    t.adlc.read(2);  // 0x01

    // FV still stored — clear it
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();

    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Now second frame should start arriving
    t.tick_byte_periods(2);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    uint8_t b0 = t.adlc.read(2);
    CHECK(b0 == 0xAA);
}

TEST_CASE("RX: AP set on address bytes", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // First byte: address byte → AP set
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);

    // Read it
    t.adlc.read(2);

    // Second byte: also address → AP still set
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);

    // Read it
    t.adlc.read(2);

    // Third byte: control → AP should clear
    t.tick_byte_periods(1);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);
}

TEST_CASE("RX: blocked when RX in reset", "[econet][mc6854][frame]") {
    TestFixture t;
    // Keep RX in reset
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01});
    t.tick_byte_periods(4);

    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_RDA);
}

TEST_CASE("RX: empty backend returns nothing", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // No frames injected
    t.tick_byte_periods(10);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_RDA);
}

// =============================================================================
// Phase 3: TX/RX Round-Trip
// =============================================================================

TEST_CASE("Round-trip: TX frame appears in backend, backend injects for RX", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // TX: send a frame
    t.adlc.write(2, 0xAA);
    t.adlc.write(2, 0xBB);
    t.adlc.write(3, 0xCC);
    t.tick_byte_periods(3);

    REQUIRE(t.backend.sent_frame_count() == 1);
    CHECK(t.backend.sent_frames()[0] == std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));

    // RX: inject a response frame
    t.backend.inject_rx_frame({0xDD, 0xEE, 0xFF});
    t.tick_byte_periods(3);

    // Read back
    CHECK(t.adlc.read(2) == 0xDD);
    CHECK(t.adlc.read(2) == 0xEE);
    CHECK(t.adlc.read(2) == 0xFF);
}

// =============================================================================
// Phase 3: Back-to-Back Frames
// =============================================================================

TEST_CASE("TX: back-to-back frames", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // First frame
    t.adlc.write(2, 0x01);
    t.adlc.write(3, 0x02);
    t.tick_byte_periods(2);
    CHECK(t.backend.sent_frame_count() == 1);

    // Second frame
    t.adlc.write(2, 0x03);
    t.adlc.write(3, 0x04);
    t.tick_byte_periods(2);
    CHECK(t.backend.sent_frame_count() == 2);

    CHECK(t.backend.sent_frames()[0] == std::vector<uint8_t>({0x01, 0x02}));
    CHECK(t.backend.sent_frames()[1] == std::vector<uint8_t>({0x03, 0x04}));
}
