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

// test_econet_nmi_delivery.cpp
//
// Tests the full NMI delivery chain for Econet TX completion:
//   ADLC irq_output() -> EconetSocket nmi_pending() -> M6502_SetDeviceNMI -> 6502 NMI
//
// Two NMI handler variants are tested:
//   1. With INTOFF/INTON in the handler (reads &FE18 at entry, &FE20 before RTI)
//   2. Without INTOFF/INTON in the handler
//
// The handler writes scout bytes in 2-byte mode, then CR2=&3F (TX_LAST_DATA)
// to trigger a CTS transition. A second NMI (for CTS) should fire and
// run the tx_complete code path which writes CR1=&82.
//
// Assembly source: tests/assets/econet/nmi_tx_test*.6502 (assembled with beebasm)

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/econet/TestBackend.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

// RAM locations used by the test programs (must match .6502 source)
constexpr uint16_t NMI_COUNT        = 0x0072;
constexpr uint16_t MAIN_LOOP_COUNT  = 0x0073;
constexpr uint16_t TX_BYTE_INDEX    = 0x0074;
constexpr uint16_t TX_MODE          = 0x0075;
constexpr uint16_t TX_DONE          = 0x0076;
constexpr uint16_t TX_COMPLETE_SR1  = 0x0077;

// Main program binary (identical for both variants)
// Assembled from &0400: INTOFF, configure ADLC, INTON, spin
static const uint8_t main_program[] = {
    0xa9, 0x00, 0x85, 0x72, 0x85, 0x73, 0x85, 0x74, 0x85, 0x75, 0x85, 0x76,
    0x85, 0x77, 0xad, 0x18, 0xfe, 0xa9, 0xe7, 0x8d, 0xa1, 0xfe, 0xa9, 0x44,
    0x8d, 0xa0, 0xfe, 0xad, 0x20, 0xfe, 0xe6, 0x73, 0x4c, 0x1e, 0x04
};

// NMI handler WITH INTOFF/INTON (81 bytes at &0D00)
// Reads &FE18 at entry (INTOFF), reads &FE20 before RTI (INTON)
static const uint8_t nmi_handler_with_intoff[] = {
    0x48, 0x8a, 0x48, 0xad, 0x18, 0xfe, 0xe6, 0x72, 0xa5, 0x75, 0xd0, 0x2c,
    0xad, 0xa0, 0xfe, 0xa6, 0x74, 0xe0, 0x04, 0xb0, 0x17, 0xbd, 0x4d, 0x0d,
    0x8d, 0xa2, 0xfe, 0xe8, 0xe0, 0x04, 0xb0, 0x07, 0xbd, 0x4d, 0x0d, 0x8d,
    0xa2, 0xfe, 0xe8, 0x86, 0x74, 0x4c, 0x46, 0x0d, 0xa9, 0x01, 0x85, 0x75,
    0xa9, 0x3f, 0x8d, 0xa1, 0xfe, 0x4c, 0x46, 0x0d, 0xad, 0xa0, 0xfe, 0x85,
    0x77, 0xa9, 0x82, 0x8d, 0xa0, 0xfe, 0xa9, 0xff, 0x85, 0x76, 0xad, 0x20,
    0xfe, 0x68, 0xaa, 0x68, 0x40, 0xdd, 0x00, 0xfe, 0x00
};

// NMI handler WITHOUT INTOFF/INTON (75 bytes at &0D00)
// No INTOFF at entry, no INTON before RTI
static const uint8_t nmi_handler_no_intoff[] = {
    0x48, 0x8a, 0x48, 0xe6, 0x72, 0xa5, 0x75, 0xd0, 0x2c, 0xad, 0xa0, 0xfe,
    0xa6, 0x74, 0xe0, 0x04, 0xb0, 0x17, 0xbd, 0x47, 0x0d, 0x8d, 0xa2, 0xfe,
    0xe8, 0xe0, 0x04, 0xb0, 0x07, 0xbd, 0x47, 0x0d, 0x8d, 0xa2, 0xfe, 0xe8,
    0x86, 0x74, 0x4c, 0x43, 0x0d, 0xa9, 0x01, 0x85, 0x75, 0xa9, 0x3f, 0x8d,
    0xa1, 0xfe, 0x4c, 0x43, 0x0d, 0xad, 0xa0, 0xfe, 0x85, 0x77, 0xa9, 0x82,
    0x8d, 0xa0, 0xfe, 0xa9, 0xff, 0x85, 0x76, 0x68, 0xaa, 0x68, 0x40, 0xdd,
    0x00, 0xfe, 0x00
};


namespace {

// Load test program into a ModelB machine.
// Sets up MOS with reset vector at &0400 and NMI vector at &0D00,
// loads main program at &0400 and NMI handler at &0D00.
void load_test_program(ModelB& machine,
                       const uint8_t* nmi_handler, size_t nmi_handler_size) {
    // Minimal MOS: NOP fill, reset vector -> &0400, NMI vector -> &0D00
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);
    mos[0x3FFC] = 0x00;  // Reset vector low -> &0400
    mos[0x3FFD] = 0x04;
    mos[0x3FFA] = 0x00;  // NMI vector low -> &0D00
    mos[0x3FFB] = 0x0D;
    machine.memory().load_mos(mos.data(), mos.size());

    // Load main program at &0400
    for (size_t i = 0; i < sizeof(main_program); ++i) {
        machine.write(0x0400 + i, main_program[i]);
    }

    // Load NMI handler at &0D00
    for (size_t i = 0; i < nmi_handler_size; ++i) {
        machine.write(0x0D00 + i, nmi_handler[i]);
    }
}

struct TestResults {
    uint8_t nmi_count;
    uint8_t main_loops;
    uint8_t byte_index;
    uint8_t tx_mode;
    uint8_t tx_done;
    uint8_t tx_complete_sr1;
    bool nmi_enable_ff;
    uint8_t adlc_cr1;
    bool adlc_irq;
};

TestResults run_tx_test(const uint8_t* nmi_handler, size_t nmi_handler_size,
                        int run_cycles = 50000) {
    ModelB machine;

    auto backend = std::make_unique<TestBackend>();
    backend->set_connected(true);
    machine.memory().econet_socket.enable(254, std::move(backend), true);

    load_test_program(machine, nmi_handler, nmi_handler_size);

    M6502_Reset(&machine.cpu());
    machine.step_instruction();  // reset sequence (7 cycles)
    machine.run(run_cycles);

    TestResults r{};
    r.nmi_count = machine.peek(NMI_COUNT);
    r.main_loops = machine.peek(MAIN_LOOP_COUNT);
    r.byte_index = machine.peek(TX_BYTE_INDEX);
    r.tx_mode = machine.peek(TX_MODE);
    r.tx_done = machine.peek(TX_DONE);
    r.tx_complete_sr1 = machine.peek(TX_COMPLETE_SR1);
    r.nmi_enable_ff = machine.memory().econet_socket.nmi_enable_ff();
    const auto* adlc = machine.memory().econet_socket.adlc();
    r.adlc_cr1 = adlc ? adlc->cr1() : 0;
    r.adlc_irq = adlc ? adlc->irq_output() : false;
    return r;
}

} // namespace


TEST_CASE("NMI-driven TX with INTOFF/INTON in handler completes full sequence",
          "[econet][nmi][machine]") {
    // The NFS ROM's NMI handler calls INTOFF at entry and INTON before RTI.
    // This creates a falling edge on /NMI each time, allowing repeated NMIs
    // even when the ADLC IRQ stays continuously asserted.
    //
    // Expected flow:
    //   1. INTON in main → TDRA NMI fires
    //   2. Handler: INTOFF, write 2 bytes, INTON, RTI
    //   3. INTON creates new edge → TDRA NMI fires again
    //   4. Handler: INTOFF, write 2 more bytes (all 4 done), INTON, RTI
    //   5. TDRA NMI fires (byte index = 4) → handler writes CR2=&3F, INTON, RTI
    //   6. CTS NMI fires → handler writes CR1=&82 → tx_done = &FF

    auto r = run_tx_test(nmi_handler_with_intoff, sizeof(nmi_handler_with_intoff));

    INFO("NMI count: " << (int)r.nmi_count);
    INFO("TX byte index: " << (int)r.byte_index);
    INFO("TX mode: " << (int)r.tx_mode);
    INFO("TX done: " << (int)r.tx_done);
    INFO("TX complete SR1: 0x" << std::hex << (int)r.tx_complete_sr1);
    INFO("Main loop iterations: " << std::dec << (int)r.main_loops);
    INFO("nmi_enable_ff: " << r.nmi_enable_ff);
    INFO("ADLC CR1: 0x" << std::hex << (int)r.adlc_cr1);
    INFO("ADLC IRQ: " << r.adlc_irq);

    // All 4 scout bytes must have been written
    CHECK(r.byte_index == 4);

    // Handler must have switched to tx_complete mode
    CHECK(r.tx_mode == 1);

    // tx_complete must have run (CTS NMI fired after CR2=&3F)
    CHECK(r.tx_done == 0xFF);

    // ADLC CR1 should be &82 (TX_RESET | RIE) after tx_complete
    CHECK(r.adlc_cr1 == 0x82);

    // Multiple NMIs should have fired (at least 4: 2x TDRA + 1x last-byte + 1x CTS)
    CHECK(r.nmi_count >= 4);
}


TEST_CASE("NMI-driven TX WITHOUT INTOFF/INTON in handler fails to complete",
          "[econet][nmi][machine]") {
    // Without INTOFF/INTON in the handler, the M6502 edge detection never
    // sees a new falling edge because nmi_pending() stays continuously true.
    // Only ONE NMI fires (the initial one from INTON in main).

    auto r = run_tx_test(nmi_handler_no_intoff, sizeof(nmi_handler_no_intoff));

    INFO("NMI count: " << (int)r.nmi_count);
    INFO("TX byte index: " << (int)r.byte_index);
    INFO("TX mode: " << (int)r.tx_mode);
    INFO("TX done: " << (int)r.tx_done);
    INFO("Main loop iterations: " << std::dec << (int)r.main_loops);
    INFO("nmi_enable_ff: " << r.nmi_enable_ff);
    INFO("ADLC CR1: 0x" << std::hex << (int)r.adlc_cr1);

    // Without INTOFF/INTON, TDRA NMIs fire (FIFO fill creates edges) but
    // the final CTS NMI does NOT fire (IRQ stays continuously asserted
    // through the TDRA→CTS transition when CR2=&3F is written)
    CHECK(r.nmi_count == 3);

    // All 4 bytes get written (TDRA toggling works for data bytes)
    CHECK(r.byte_index == 4);

    // tx_complete never ran
    CHECK(r.tx_done == 0x00);

    // ADLC CR1 is still &44 (never switched to &82)
    CHECK(r.adlc_cr1 == 0x44);
}


TEST_CASE("EconetSocket nmi_pending tracks INTOFF/INTON correctly", "[econet][nmi]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    backend->set_connected(true);
    socket.enable(254, std::move(backend), true);

    // Configure ADLC for TX
    socket.write_adlc(1, 0xE7);  // CR2: RTS | CLR_TX_ST | CLR_RX_ST | ...
    socket.write_adlc(0, 0x44);  // CR1: RX_RESET | TIE

    for (int i = 0; i < 10; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    // NMI enable is false by default → nmi_pending is false
    CHECK(!socket.nmi_enable_ff());
    CHECK(!socket.nmi_pending());

    // ADLC IRQ should be active (TDRA with TIE)
    CHECK(socket.adlc()->irq_output());

    // INTON
    socket.on_inton();
    CHECK(socket.nmi_enable_ff());
    CHECK(socket.nmi_pending());

    // INTOFF
    socket.read_station_id(0);
    CHECK(!socket.nmi_enable_ff());
    CHECK(!socket.nmi_pending());

    // INTON again — should re-enable
    socket.on_inton();
    CHECK(socket.nmi_enable_ff());
    CHECK(socket.nmi_pending());

    // Write bytes and TX_LAST_DATA while INTOFF
    socket.read_station_id(0);  // INTOFF
    socket.write_adlc(2, 0xDD);
    socket.write_adlc(2, 0x00);
    socket.write_adlc(2, 0xFE);
    socket.write_adlc(2, 0x00);
    socket.write_adlc(1, 0x3F);  // TX_LAST_DATA

    for (int i = 0; i < 10; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    // IRQ should be active (CTS with TIE)
    CHECK(socket.adlc()->irq_output());

    // But nmi_pending is false (INTOFF)
    CHECK(!socket.nmi_pending());

    // INTON → nmi_pending should become true
    socket.on_inton();
    CHECK(socket.nmi_pending());
}
