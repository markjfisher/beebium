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

#pragma once

#include "TubeShared.hpp"
#include "TubeUla.hpp"  // for FLAG_* constants

#include <cassert>
#include <cstdint>

namespace beebium {

// Parasite-side adapter for the Tube ULA over shared memory.
//
// This is the parasite counterpart of TubeHostPort. It reads from H-to-P
// registers (written by the host) and writes to P-to-H registers (read by
// the host). It also computes PIRQ and PNMI from the shared state.
//
// The parasite processor's Tube registers are mapped at &FEF8-&FEFF in the
// parasite address space (offsets 0-7, mirrored).
//
// Ownership: TubeParasitePort does NOT own the TubeShared memory. The caller
// is responsible for its lifetime.
//
// Memory ordering: stores use release, loads use acquire.

class TubeParasitePort {
public:
    explicit TubeParasitePort(TubeShared* shared)
        : shared_(shared)
    {
        assert(shared_ != nullptr);
    }

    // --- Parasite register access (offsets 0-7) ---

    uint8_t parasite_read(uint8_t offset);
    uint8_t parasite_peek(uint8_t offset) const;
    void parasite_write(uint8_t offset, uint8_t value);

    // --- Interrupt outputs ---

    // PIRQ: (I=1 AND R1 H-to-P has data) OR (J=1 AND R4 H-to-P has data).
    bool pirq() const;

    // PNMI (edge-detected): latched rising edge from R3 activity when M=1.
    // Only updates during parasite_read/parasite_write calls.
    // Prefer pnmi_level() when driving an M6502 NMI line, since the M6502
    // performs its own edge detection.
    bool pnmi() const;

    // PNMI level: raw combinational output, recomputed on each call from
    // shared state. Active when M=1 AND (R3 H-to-P has data OR R3 P-to-H
    // has space). Use this with M6502_SetDeviceNMI, which does its own
    // edge detection internally.
    bool pnmi_level() const;

    // --- Reset ---

    // Clear all register data. Called when the parasite receives a reset
    // command via the lifecycle mailbox.
    void reset();

    // --- Accessors for testing ---

    TubeShared* shared() { return shared_; }
    const TubeShared* shared() const { return shared_; }

    uint8_t control_flags() const {
        return shared_->control_flags.load(std::memory_order_acquire);
    }

private:
    // Read a byte from the R3 H-to-P register (shift down).
    uint8_t dequeue_r3_h2p();

    // Enqueue a byte to the R1 P-to-H FIFO.
    void enqueue_r1_p2h(uint8_t value);

    // Enqueue a byte to the R3 P-to-H register.
    void enqueue_r3_p2h(uint8_t value);

    // Recompute PNMI level and detect edges.
    void update_pnmi();

    TubeShared* shared_;

    // PNMI edge detection state (parasite-local, not shared).
    bool prev_pnmi_ = false;
    bool pnmi_edge_ = false;
};

}  // namespace beebium
