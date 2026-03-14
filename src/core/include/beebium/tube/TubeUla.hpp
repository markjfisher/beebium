// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#pragma once

#include "TubeHostBackend.hpp"

#include <array>
#include <cstdint>

namespace beebium {

// Tube ULA register model.
//
// Models the Acorn Tube ULA's four register sets, control flags, and
// interrupt outputs. This is a plain in-process implementation -- no shared
// memory, no atomics. It is the authoritative FIFO model used for both
// in-process testing and as the basis for the shared-memory variant.
//
// The host and parasite sides see different views of the same registers:
// different status bits, different read/write targets. The caller selects
// the perspective by calling host_read/host_write or parasite_read/
// parasite_write.

class TubeUla : public TubeHostBackend {
public:
    // Status flag bits (bits 7 and 6 of status register reads)
    static constexpr uint8_t DATA_AVAILABLE = 0x80;  // bit 7
    static constexpr uint8_t SPACE_AVAILABLE = 0x40;  // bit 6

    // Control flag bits (written via host offset 0)
    static constexpr uint8_t FLAG_S = 0x80;  // set/clear mode select
    static constexpr uint8_t FLAG_T = 0x40;  // soft reset (clear all registers)
    static constexpr uint8_t FLAG_P = 0x20;  // parasite reset
    static constexpr uint8_t FLAG_V = 0x10;  // two-byte mode for R3
    static constexpr uint8_t FLAG_M = 0x08;  // enable PNMI from R3
    static constexpr uint8_t FLAG_J = 0x04;  // enable PIRQ from R4
    static constexpr uint8_t FLAG_I = 0x02;  // enable PIRQ from R1
    static constexpr uint8_t FLAG_Q = 0x01;  // enable HIRQ from R4

    TubeUla();

    // Full hardware reset (HRST) -- clears everything including control flags.
    void reset() override;

    // Host-side register access (offsets 0-7, mirrored from &FEE0-&FEE7).
    uint8_t host_read(uint8_t offset) override;
    void host_write(uint8_t offset, uint8_t value) override;

    // Parasite-side register access (offsets 0-7, mirrored from &FEF8-&FEFF).
    uint8_t parasite_read(uint8_t offset);
    void parasite_write(uint8_t offset, uint8_t value);

    // Interrupt outputs (active high in this model; caller inverts if needed).
    // These use the Tube-specific names from Application Note 004:
    //   hirq -- Host IRQ (active when Q=1 and R4 has P-to-H data)
    //   pirq -- Parasite IRQ (active when I=1 and R1 has data, or J=1 and R4 has data)
    //   pnmi -- Parasite NMI (edge-triggered from R3 activity when M=1)
    // TubeSocket adapts hirq() to the generic IrqSource::irq_pending() interface.
    bool hirq() const override;
    bool pirq() const;
    bool pnmi() const;

    // Read control flags (bits 0-5: Q, I, J, M, V, P).
    uint8_t control_flags() const { return control_flags_; }

    // Test whether the parasite reset line is currently asserted.
    bool parasite_reset_active() const { return (control_flags_ & FLAG_P) != 0; }

    // Bus stretching: returns true if the last host access could not complete
    // because the register was full (write) or empty (read).
    bool stretched() const override { return stretched_; }

    // Access the NMI edge detector state (parasite-local).
    bool prev_pnmi() const { return prev_pnmi_; }

private:
    // Soft reset (T flag) -- clears all register data but preserves control flags.
    void soft_reset();

    // Recompute interrupt outputs after any register access.
    void update_interrupts();

    // Control flag register (bits 0-5: Q, I, J, M, V, P).
    uint8_t control_flags_ = 0;

    // Register 1: H-to-P is a 1-byte latch; P-to-H is a 24-byte FIFO.
    struct {
        uint8_t h2p_data = 0;
        bool h2p_available = false;  // data waiting for parasite to read
        bool h2p_full = false;       // host cannot write (parasite hasn't read yet)

        std::array<uint8_t, 24> p2h_fifo{};
        uint8_t p2h_head = 0;
        uint8_t p2h_tail = 0;
        uint8_t p2h_count = 0;
    } r1_;

    // Register 2: 1-byte latch in each direction.
    struct {
        uint8_t h2p_data = 0;
        bool h2p_available = false;
        bool h2p_full = false;

        uint8_t p2h_data = 0;
        bool p2h_available = false;
        bool p2h_full = false;
    } r2_;

    // Register 3: 2-byte FIFO in each direction, configurable as 1 or 2-byte.
    //
    // The pending flags track whether a complete transfer has been deposited
    // and is awaiting consumption.  They provide hysteresis so that the
    // space-available / data-available status bits remain correct when the
    // count is between 0 and threshold during a multi-byte transfer.
    //
    //   pending = false  ->  writer phase (space available)
    //   pending = true   ->  reader phase (data available)
    //
    // Transitions: set when count reaches threshold on write; cleared when
    // count reaches 0 on read.
    struct {
        std::array<uint8_t, 2> h2p_data{};
        uint8_t h2p_count = 0;
        bool h2p_pending = false;  // complete H-to-P transfer awaiting parasite read

        std::array<uint8_t, 2> p2h_data{};
        uint8_t p2h_count = 0;
        bool p2h_pending = false;  // complete P-to-H transfer awaiting host read
    } r3_;

    // Register 4: 1-byte latch in each direction.
    struct {
        uint8_t h2p_data = 0;
        bool h2p_available = false;
        bool h2p_full = false;

        uint8_t p2h_data = 0;
        bool p2h_available = false;
        bool p2h_full = false;
    } r4_;

    // Bus stretching state.
    //
    // When the host writes to a full H-to-P register (R1, R3, R4) the real
    // Tube ULA halts the host CPU until the parasite drains the register.
    // In this in-process model the write is buffered and stretched_ is set.
    // The pending write completes automatically when the parasite reads the
    // blocked register.
    bool stretched_ = false;
    bool pending_write_ = false;
    uint8_t pending_write_offset_ = 0;
    uint8_t pending_write_value_ = 0;

    // Try to complete a pending write after the parasite drains a register.
    void try_complete_pending_write();

    // Interrupt output state (cached, recomputed after each access).
    bool hirq_ = false;
    bool pirq_ = false;
    bool pnmi_level_ = false;  // current level of PNMI condition
    bool prev_pnmi_ = false;   // for edge detection
    bool pnmi_edge_ = false;   // latched rising edge
};

}  // namespace beebium
