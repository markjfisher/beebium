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

#include <beebium/tube/TubeUla.hpp>

namespace beebium {

TubeUla::TubeUla()
{
    reset();
}

void TubeUla::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    control_flags_ = 0;
    stretched_ = false;
    pending_write_ = false;
    soft_reset();
}

void TubeUla::soft_reset()
{
    // R1: clear latch and FIFO.
    r1_.h2p_data = 0;
    r1_.h2p_available = false;
    r1_.h2p_full = false;
    r1_.p2h_fifo.fill(0);
    r1_.p2h_head = 0;
    r1_.p2h_tail = 0;
    r1_.p2h_count = 0;

    // R2: clear latches.
    r2_.h2p_data = 0;
    r2_.h2p_available = false;
    r2_.h2p_full = false;
    r2_.p2h_data = 0;
    r2_.p2h_available = false;
    r2_.p2h_full = false;

    // R3: clear FIFOs. P-to-H gets one dummy byte to prevent spurious PNMI.
    r3_.h2p_data.fill(0);
    r3_.h2p_count = 0;
    r3_.h2p_pending = false;
    r3_.p2h_data.fill(0);
    r3_.p2h_count = 1;  // dummy byte
    r3_.p2h_pending = true;  // dummy byte counts as a complete transfer

    // R4: clear latches.
    r4_.h2p_data = 0;
    r4_.h2p_available = false;
    r4_.h2p_full = false;
    r4_.p2h_data = 0;
    r4_.p2h_available = false;
    r4_.p2h_full = false;

    // Bus stretching state.
    stretched_ = false;
    pending_write_ = false;

    // Interrupt state.
    hirq_.store(false, std::memory_order_relaxed);
    pirq_.store(false, std::memory_order_relaxed);
    pnmi_level_.store(false, std::memory_order_relaxed);
    prev_pnmi_ = false;
    pnmi_edge_.store(false, std::memory_order_relaxed);

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Host-side register access
// ---------------------------------------------------------------------------

uint8_t TubeUla::host_read(uint8_t offset)
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        // R1 status + control flags.
        // Bit 7: R1 P-to-H data available (host can read).
        // Bit 6: R1 H-to-P not full (host can write).
        // Bits 5-0: control flags (P, V, M, J, I, Q).
        result = control_flags_ & 0x3F;
        if (r1_.p2h_count > 0)
            result |= DATA_AVAILABLE;
        if (!r1_.h2p_full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from P-to-H 24-byte FIFO.
        if (r1_.p2h_count > 0) {
            result = r1_.p2h_fifo[r1_.p2h_head];
            r1_.p2h_head = (r1_.p2h_head + 1) % 24;
            r1_.p2h_count--;
        }
        break;
    }

    case 2: {
        // R2 status.
        // Bit 7: R2 P-to-H data available.
        // Bit 6: R2 H-to-P not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (r2_.p2h_available)
            result |= DATA_AVAILABLE;
        if (!r2_.h2p_full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from P-to-H latch.
        result = r2_.p2h_data;
        if (r2_.p2h_available) {
            r2_.p2h_available = false;
            r2_.p2h_full = false;
        }
        break;
    }

    case 4: {
        // R3 status (host perspective).
        // Bit 7: R3 P-to-H data available (pending transfer or count >= threshold).
        // Bit 6: R3 H-to-P space available (no pending transfer awaiting read).
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        if (r3_.p2h_pending || r3_.p2h_count >= threshold)
            result |= DATA_AVAILABLE;
        if (!r3_.h2p_pending)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 5: {
        // R3 data: read from P-to-H FIFO.
        if (r3_.p2h_count > 0) {
            result = r3_.p2h_data[0];
            r3_.p2h_data[0] = r3_.p2h_data[1];
            r3_.p2h_data[1] = 0;
            r3_.p2h_count--;
            if (r3_.p2h_count == 0)
                r3_.p2h_pending = false;
        }
        break;
    }

    case 6: {
        // R4 status.
        // Bit 7: R4 P-to-H data available.
        // Bit 6: R4 H-to-P not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (r4_.p2h_available)
            result |= DATA_AVAILABLE;
        if (!r4_.h2p_full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from P-to-H latch.
        result = r4_.p2h_data;
        if (r4_.p2h_available) {
            r4_.p2h_available = false;
            r4_.p2h_full = false;
        }
        break;
    }
    }

    update_interrupts();
    return result;
}

uint8_t TubeUla::host_peek(uint8_t offset) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_ & 0x3F;
        if (r1_.p2h_count > 0)
            result |= DATA_AVAILABLE;
        if (!r1_.h2p_full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 1:
        result = (r1_.p2h_count > 0) ? r1_.p2h_fifo[r1_.p2h_head] : 0;
        break;
    case 2: {
        result = 0x3F;
        if (r2_.p2h_available)
            result |= DATA_AVAILABLE;
        if (!r2_.h2p_full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = r2_.p2h_data;
        break;
    case 4: {
        result = 0x3F;
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        if (r3_.p2h_pending || r3_.p2h_count >= threshold)
            result |= DATA_AVAILABLE;
        if (!r3_.h2p_pending)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 5:
        result = (r3_.p2h_count > 0) ? r3_.p2h_data[0] : 0;
        break;
    case 6: {
        result = 0x3F;
        if (r4_.p2h_available)
            result |= DATA_AVAILABLE;
        if (!r4_.h2p_full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = r4_.p2h_data;
        break;
    }

    return result;
}

uint8_t TubeUla::parasite_peek(uint8_t offset) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_ & 0x3F;
        if (r1_.h2p_available)
            result |= DATA_AVAILABLE;
        if (r1_.p2h_count < 24)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 1:
        result = r1_.h2p_data;
        break;
    case 2: {
        result = 0x3F;
        if (r2_.h2p_available)
            result |= DATA_AVAILABLE;
        if (!r2_.p2h_full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = r2_.h2p_data;
        break;
    case 4: {
        result = 0x1F;
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        bool h2p_data = r3_.h2p_pending || r3_.h2p_count >= threshold;
        bool p2h_space = !r3_.p2h_pending;
        if (h2p_data)
            result |= DATA_AVAILABLE;
        if (p2h_space)
            result |= SPACE_AVAILABLE;
        if (h2p_data || p2h_space)
            result |= 0x20;  // N flag
        break;
    }
    case 5:
        result = (r3_.h2p_count > 0) ? r3_.h2p_data[0] : 0;
        break;
    case 6: {
        result = 0x3F;
        if (r4_.h2p_available)
            result |= DATA_AVAILABLE;
        if (!r4_.p2h_full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = r4_.h2p_data;
        break;
    }

    return result;
}

void TubeUla::host_write(uint8_t offset, uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (offset & 7) {
    case 0: {
        // Control flag register.
        if (value & FLAG_S) {
            // Set mode: OR specified bits into control flags.
            // T flag (bit 6) triggers soft reset every time it is written.
            // T is not stored in the control flags register -- it is an action,
            // not a persistent state. B-Em confirms: r1stat only stores bits 0-5.
            if (value & FLAG_T)
                soft_reset();
            control_flags_ |= (value & 0x3F);
        } else {
            // Clear mode: AND out specified bits.
            control_flags_ &= ~(value & 0x3F);
        }
        break;
    }

    case 1: {
        // R1 data: write to H-to-P latch.
        // Bus stretching: if the latch is full (parasite hasn't read the
        // previous byte), the real Tube ULA halts the host CPU. We buffer
        // the write and set stretched_.
        if (r1_.h2p_full) {
            stretched_ = true;
            pending_write_ = true;
            pending_write_offset_ = offset;
            pending_write_value_ = value;
            return;
        }
        r1_.h2p_data = value;
        r1_.h2p_available = true;
        r1_.h2p_full = true;
        break;
    }

    case 2:
        // R2 status: no writable effect from host.
        break;

    case 3: {
        // R2 data: write to H-to-P latch.
        // No bus stretching on R2 (Application Note 004).
        r2_.h2p_data = value;
        r2_.h2p_available = true;
        r2_.h2p_full = true;
        break;
    }

    case 4:
        // R3 status: no writable effect from host.
        break;

    case 5: {
        // R3 data: write to H-to-P FIFO.
        // Bus stretching: if the 2-byte FIFO is physically full, the real
        // Tube ULA halts the host CPU. We buffer the write and set stretched_.
        if (r3_.h2p_count >= 2) {
            stretched_ = true;
            pending_write_ = true;
            pending_write_offset_ = offset;
            pending_write_value_ = value;
            return;
        }
        r3_.h2p_data[r3_.h2p_count] = value;
        r3_.h2p_count++;
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        if (r3_.h2p_count >= threshold)
            r3_.h2p_pending = true;
        break;
    }

    case 6:
        // R4 status: no writable effect from host.
        break;

    case 7: {
        // R4 data: write to H-to-P latch.
        // Bus stretching: same as R1.
        if (r4_.h2p_full) {
            stretched_ = true;
            pending_write_ = true;
            pending_write_offset_ = offset;
            pending_write_value_ = value;
            return;
        }
        r4_.h2p_data = value;
        r4_.h2p_available = true;
        r4_.h2p_full = true;
        break;
    }
    }

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Parasite-side register access
// ---------------------------------------------------------------------------

uint8_t TubeUla::parasite_read(uint8_t offset)
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        // R1 status from parasite perspective.
        // Bit 7: R1 H-to-P data available (parasite can read).
        // Bit 6: R1 P-to-H FIFO not full (parasite can write).
        // Bits 5-0: control flags.
        result = control_flags_ & 0x3F;
        if (r1_.h2p_available)
            result |= DATA_AVAILABLE;
        if (r1_.p2h_count < 24)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from H-to-P latch.
        result = r1_.h2p_data;
        if (r1_.h2p_available) {
            r1_.h2p_available = false;
            r1_.h2p_full = false;
        }
        break;
    }

    case 2: {
        // R2 status from parasite perspective.
        // Bit 7: R2 H-to-P data available.
        // Bit 6: R2 P-to-H not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (r2_.h2p_available)
            result |= DATA_AVAILABLE;
        if (!r2_.p2h_full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from H-to-P latch.
        result = r2_.h2p_data;
        if (r2_.h2p_available) {
            r2_.h2p_available = false;
            r2_.h2p_full = false;
        }
        break;
    }

    case 4: {
        // R3 status (parasite perspective).
        // Bit 7: R3 H-to-P data available (pending transfer or count >= threshold).
        // Bit 6: R3 P-to-H space available (no pending transfer awaiting read).
        // Bit 5: N flag -- NMI action required (raw condition, independent of M).
        // Bits 4-0: read as 1 (App Note 004, note 11).
        result = 0x1F;
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        bool h2p_data = r3_.h2p_pending || r3_.h2p_count >= threshold;
        bool p2h_space = !r3_.p2h_pending;
        if (h2p_data)
            result |= DATA_AVAILABLE;
        if (p2h_space)
            result |= SPACE_AVAILABLE;
        if (h2p_data || p2h_space)
            result |= 0x20;  // N flag
        break;
    }

    case 5: {
        // R3 data: read from H-to-P FIFO.
        if (r3_.h2p_count > 0) {
            result = r3_.h2p_data[0];
            r3_.h2p_data[0] = r3_.h2p_data[1];
            r3_.h2p_data[1] = 0;
            r3_.h2p_count--;
            if (r3_.h2p_count == 0)
                r3_.h2p_pending = false;
        }
        break;
    }

    case 6: {
        // R4 status from parasite perspective.
        // Bit 7: R4 H-to-P data available.
        // Bit 6: R4 P-to-H not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (r4_.h2p_available)
            result |= DATA_AVAILABLE;
        if (!r4_.p2h_full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from H-to-P latch.
        result = r4_.h2p_data;
        if (r4_.h2p_available) {
            r4_.h2p_available = false;
            r4_.h2p_full = false;
        }
        break;
    }
    }

    // If the host is stretched on a pending write, check whether draining
    // this register allows the pending write to complete.
    if (pending_write_)
        try_complete_pending_write();

    update_interrupts();
    return result;
}

void TubeUla::parasite_write(uint8_t offset, uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (offset & 7) {
    case 0:
        // Parasite cannot write to control register.
        break;

    case 1: {
        // R1 data: write to P-to-H 24-byte FIFO.
        if (r1_.p2h_count < 24) {
            r1_.p2h_fifo[r1_.p2h_tail] = value;
            r1_.p2h_tail = (r1_.p2h_tail + 1) % 24;
            r1_.p2h_count++;
        }
        break;
    }

    case 2:
        // R2 status: no writable effect from parasite.
        break;

    case 3: {
        // R2 data: write to P-to-H latch.
        r2_.p2h_data = value;
        r2_.p2h_available = true;
        r2_.p2h_full = true;
        break;
    }

    case 4:
        // R3 status: no writable effect from parasite.
        break;

    case 5: {
        // R3 data: write to P-to-H FIFO.
        if (r3_.p2h_count < 2) {
            r3_.p2h_data[r3_.p2h_count] = value;
            r3_.p2h_count++;
            uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
            if (r3_.p2h_count >= threshold)
                r3_.p2h_pending = true;
        }
        break;
    }

    case 6:
        // R4 status: no writable effect from parasite.
        break;

    case 7: {
        // R4 data: write to P-to-H latch.
        r4_.p2h_data = value;
        r4_.p2h_available = true;
        r4_.p2h_full = true;
        break;
    }
    }

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Bus stretching: pending write completion
// ---------------------------------------------------------------------------

void TubeUla::try_complete_pending_write()
{
    switch (pending_write_offset_ & 7) {
    case 1:
        if (!r1_.h2p_full) {
            r1_.h2p_data = pending_write_value_;
            r1_.h2p_available = true;
            r1_.h2p_full = true;
            pending_write_ = false;
            stretched_ = false;
        }
        break;
    case 5:
        if (r3_.h2p_count < 2) {
            r3_.h2p_data[r3_.h2p_count] = pending_write_value_;
            r3_.h2p_count++;
            uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
            if (r3_.h2p_count >= threshold)
                r3_.h2p_pending = true;
            pending_write_ = false;
            stretched_ = false;
        }
        break;
    case 7:
        if (!r4_.h2p_full) {
            r4_.h2p_data = pending_write_value_;
            r4_.h2p_available = true;
            r4_.h2p_full = true;
            pending_write_ = false;
            stretched_ = false;
        }
        break;
    default:
        // Should not happen -- only R1, R3, R4 can stretch.
        pending_write_ = false;
        stretched_ = false;
        break;
    }
}

// ---------------------------------------------------------------------------
// Interrupt computation
// ---------------------------------------------------------------------------

bool TubeUla::hirq() const
{
    return hirq_.load(std::memory_order_acquire);
}

bool TubeUla::pirq() const
{
    return pirq_.load(std::memory_order_acquire);
}

bool TubeUla::pnmi() const
{
    return pnmi_edge_.load(std::memory_order_acquire);
}

bool TubeUla::pnmi_level() const
{
    return pnmi_level_.load(std::memory_order_acquire);
}

void TubeUla::update_interrupts()
{
    // Called under mutex_. Computes interrupt outputs and stores them
    // to atomics for lock-free polling by hirq()/pirq()/pnmi().

    // HIRQ: Q=1 AND R4 P-to-H has data available.
    hirq_.store((control_flags_ & FLAG_Q) && r4_.p2h_available,
                std::memory_order_release);

    // PIRQ: (I=1 AND R1 H-to-P has data) OR (J=1 AND R4 H-to-P has data).
    pirq_.store(((control_flags_ & FLAG_I) && r1_.h2p_available)
             || ((control_flags_ & FLAG_J) && r4_.h2p_available),
                std::memory_order_release);

    // PNMI level computation.
    bool new_pnmi = false;
    if (control_flags_ & FLAG_M) {
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        // NMI when: H-to-P has a complete transfer, OR P-to-H is ready for writing.
        new_pnmi = (r3_.h2p_pending || r3_.h2p_count >= threshold)
                || !r3_.p2h_pending;
    }

    // Edge detection: fire on 0-to-1 transition.
    if (new_pnmi && !prev_pnmi_)
        pnmi_edge_.store(true, std::memory_order_release);

    // Clear edge when the level drops (cause removed).
    if (!new_pnmi)
        pnmi_edge_.store(false, std::memory_order_release);

    prev_pnmi_ = new_pnmi;
    pnmi_level_.store(new_pnmi, std::memory_order_release);
}

}  // namespace beebium
