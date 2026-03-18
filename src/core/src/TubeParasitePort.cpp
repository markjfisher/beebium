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

#include <beebium/tube/TubeParasitePort.hpp>

namespace beebium {

// ---------------------------------------------------------------------------
// Parasite-side register read
// ---------------------------------------------------------------------------

uint8_t TubeParasitePort::parasite_read(uint8_t offset)
{
    uint8_t result = 0;
    auto flags = shared_->control_flags.load(std::memory_order_acquire);

    switch (offset & 7) {
    case 0: {
        // R1 status from parasite perspective.
        // Bit 7: R1 H-to-P data available (parasite can read).
        // Bit 6: R1 P-to-H FIFO not full (parasite can write).
        // Bits 5-0: control flags.
        result = flags & 0x3F;
        if (shared_->r1_h2p.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r1_p2h.count.load(std::memory_order_acquire) < 24)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from H-to-P latch (deferred ready clear).
        //
        // The value is captured immediately but the ready flag is NOT cleared
        // until complete_cycle() runs at the END of this CPU tick. This
        // models the real Tube ULA which holds the latch stable for the
        // entire bus cycle. Re-reads within the same tick return the cached
        // value, preventing TOCTOU races in multi-cycle instructions.
        if (pending_clear_mask_ & 0x01) {
            result = cached_r1_;
        } else {
            auto was_ready = shared_->r1_h2p.ready.load(std::memory_order_acquire);
            cached_r1_ = shared_->r1_h2p.value.load(std::memory_order_relaxed);
            result = cached_r1_;
            if (was_ready != 0) {
                pending_clear_mask_ |= 0x01;
                shared_->counters.r1_h2p_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        break;
    }

    case 2: {
        // R2 status from parasite perspective.
        // Bit 7: R2 H-to-P data available.
        // Bit 6: R2 P-to-H not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (shared_->r2_h2p.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r2_p2h.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from H-to-P latch (deferred ready clear).
        if (pending_clear_mask_ & 0x02) {
            result = cached_r2_;
        } else {
            auto was_ready = shared_->r2_h2p.ready.load(std::memory_order_acquire);
            cached_r2_ = shared_->r2_h2p.value.load(std::memory_order_relaxed);
            result = cached_r2_;
            if (was_ready != 0) {
                pending_clear_mask_ |= 0x02;
                shared_->counters.r2_h2p_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        break;
    }

    case 4: {
        // R3 status (parasite perspective).
        // Bit 7: R3 H-to-P data available (count >= threshold).
        // Bit 6: R3 P-to-H space available (count < threshold).
        // Bit 5: N flag -- NMI action required (raw condition, independent of M).
        // Bits 4-0: read as 1 (App Note 004, note 11).
        result = 0x1F;
        uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;
        bool h2p_data = shared_->r3_h2p.count.load(std::memory_order_acquire) >= threshold;
        bool p2h_space = shared_->r3_p2h.count.load(std::memory_order_acquire) < threshold;
        if (h2p_data)
            result |= TubeUla::DATA_AVAILABLE;
        if (p2h_space)
            result |= TubeUla::SPACE_AVAILABLE;
        if (h2p_data || p2h_space)
            result |= 0x20;  // N flag
        break;
    }

    case 5: {
        // R3 data: read from H-to-P register (deferred dequeue).
        if (pending_clear_mask_ & 0x04) {
            result = cached_r3_;
        } else {
            uint8_t count = shared_->r3_h2p.count.load(std::memory_order_acquire);
            if (count > 0) {
                uint8_t head = shared_->r3_h2p.head.load(std::memory_order_relaxed);
                cached_r3_ = shared_->r3_h2p.data[head].load(std::memory_order_acquire);
                pending_clear_mask_ |= 0x04;
                shared_->counters.r3_h2p_reads.fetch_add(1, std::memory_order_relaxed);
            } else {
                cached_r3_ = shared_->host_data_latch.load(std::memory_order_relaxed);
            }
            result = cached_r3_;
        }
        break;
    }

    case 6: {
        // R4 status from parasite perspective.
        // Bit 7: R4 H-to-P data available.
        // Bit 6: R4 P-to-H not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (shared_->r4_h2p.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r4_p2h.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from H-to-P latch (deferred ready clear).
        if (pending_clear_mask_ & 0x08) {
            result = cached_r4_;
        } else {
            auto was_ready = shared_->r4_h2p.ready.load(std::memory_order_acquire);
            cached_r4_ = shared_->r4_h2p.value.load(std::memory_order_relaxed);
            result = cached_r4_;
            if (was_ready != 0) {
                pending_clear_mask_ |= 0x08;
                shared_->counters.r4_h2p_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        break;
    }
    }

    update_pnmi();
    return result;
}

// ---------------------------------------------------------------------------
// Parasite-side side-effect-free register read (debugger inspection)
// ---------------------------------------------------------------------------

uint8_t TubeParasitePort::parasite_peek(uint8_t offset) const
{
    uint8_t result = 0;
    auto flags = shared_->control_flags.load(std::memory_order_acquire);

    switch (offset & 7) {
    case 0: {
        result = flags & 0x3F;
        if (shared_->r1_h2p.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r1_p2h.count.load(std::memory_order_acquire) < 24)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 1:
        result = shared_->r1_h2p.value.load(std::memory_order_acquire);
        break;
    case 2: {
        result = 0x3F;
        if (shared_->r2_h2p.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r2_p2h.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = shared_->r2_h2p.value.load(std::memory_order_acquire);
        break;
    case 4: {
        result = 0x1F;
        uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;
        bool h2p_data = shared_->r3_h2p.count.load(std::memory_order_acquire) >= threshold;
        bool p2h_space = shared_->r3_p2h.count.load(std::memory_order_acquire) < threshold;
        if (h2p_data)
            result |= TubeUla::DATA_AVAILABLE;
        if (p2h_space)
            result |= TubeUla::SPACE_AVAILABLE;
        if (h2p_data || p2h_space)
            result |= 0x20;  // N flag
        break;
    }
    case 5: {
        uint8_t count = shared_->r3_h2p.count.load(std::memory_order_acquire);
        if (count > 0) {
            uint8_t head = shared_->r3_h2p.head.load(std::memory_order_relaxed);
            result = shared_->r3_h2p.data[head].load(std::memory_order_acquire);
        }
        break;
    }
    case 6: {
        result = 0x3F;
        if (shared_->r4_h2p.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r4_p2h.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = shared_->r4_h2p.value.load(std::memory_order_acquire);
        break;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Parasite-side register write
// ---------------------------------------------------------------------------

void TubeParasitePort::parasite_write(uint8_t offset, uint8_t value)
{
    // Update the parasite data bus latch.
    shared_->parasite_data_latch.store(value, std::memory_order_relaxed);
    switch (offset & 7) {
    case 0:
        // Parasite cannot write to control register.
        break;

    case 1: {
        // R1 data: write to P-to-H 24-byte FIFO.
        enqueue_r1_p2h(value);
        shared_->counters.r1_p2h_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 2:
        // R2 status: no writable effect from parasite.
        break;

    case 3: {
        // R2 data: write to P-to-H latch.
        // No bus stretching on R2 (App Note 004).
        shared_->r2_p2h.value.store(value, std::memory_order_relaxed);
        shared_->r2_p2h.ready.store(1, std::memory_order_release);
        shared_->counters.r2_p2h_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 4:
        // R3 status: no writable effect from parasite.
        break;

    case 5: {
        // R3 data: write to P-to-H register.
        enqueue_r3_p2h(value);
        shared_->counters.r3_p2h_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 6:
        // R4 status: no writable effect from parasite.
        break;

    case 7: {
        // R4 data: write to P-to-H latch.
        shared_->r4_p2h.value.store(value, std::memory_order_relaxed);
        shared_->r4_p2h.ready.store(1, std::memory_order_release);
        shared_->counters.r4_p2h_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    }

    update_pnmi();
}

// ---------------------------------------------------------------------------
// PIRQ computation
// ---------------------------------------------------------------------------

bool TubeParasitePort::pirq() const
{
    auto flags = shared_->control_flags.load(std::memory_order_acquire);
    bool from_r1 = (flags & TubeUla::FLAG_I)
                && shared_->r1_h2p.ready.load(std::memory_order_acquire) != 0;
    bool from_r4 = (flags & TubeUla::FLAG_J)
                && shared_->r4_h2p.ready.load(std::memory_order_acquire) != 0;
    return from_r1 || from_r4;
}

// ---------------------------------------------------------------------------
// PNMI computation
// ---------------------------------------------------------------------------

bool TubeParasitePort::pnmi() const
{
    return pnmi_edge_;
}

bool TubeParasitePort::pnmi_level() const
{
    auto flags = shared_->control_flags.load(std::memory_order_acquire);

    if (!(flags & TubeUla::FLAG_M))
        return false;

    // PNMI fires when M=1 AND either:
    //   - R3 H-to-P has data available (count >= threshold), OR
    //   - R3 P-to-H FIFO is empty (count < threshold) -- space available
    //
    // This matches jsbeeb (tube.js line 109) and the Tube Application Note:
    //   "M=1, V=0: 1 or 2 bytes in H-to-P R3 FIFO or 0 bytes in P-to-H R3 FIFO"
    //   "M=1, V=1: 2 bytes in H-to-P R3 FIFO"
    //
    // Note: B-Em only checks H-to-P data, but jsbeeb checks both and works
    // with CE2023. The Tube Application Note explicitly describes the P-to-H
    // space condition for 1-byte mode.
    uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;
    bool h2p_data = shared_->r3_h2p.count.load(std::memory_order_acquire) >= threshold;
    bool p2h_space = shared_->r3_p2h.count.load(std::memory_order_acquire) < threshold;
    return h2p_data || p2h_space;
}

void TubeParasitePort::update_pnmi()
{
    bool new_pnmi = pnmi_level();

    // Edge detection: fire on 0-to-1 transition.
    if (new_pnmi && !prev_pnmi_)
        pnmi_edge_ = true;

    // Clear edge when the level drops (cause removed).
    if (!new_pnmi)
        pnmi_edge_ = false;

    prev_pnmi_ = new_pnmi;
}

// ---------------------------------------------------------------------------
// Deferred bus cycle completion
// ---------------------------------------------------------------------------

void TubeParasitePort::complete_cycle()
{
    if (pending_clear_mask_ == 0) return;

    if (pending_clear_mask_ & 0x01)
        shared_->r1_h2p.ready.store(0, std::memory_order_release);

    if (pending_clear_mask_ & 0x02)
        shared_->r2_h2p.ready.store(0, std::memory_order_release);

    if (pending_clear_mask_ & 0x04) {
        uint8_t head = shared_->r3_h2p.head.load(std::memory_order_relaxed);
        shared_->r3_h2p.head.store(head ^ 1, std::memory_order_relaxed);
        shared_->r3_h2p.count.fetch_sub(1, std::memory_order_release);
    }

    if (pending_clear_mask_ & 0x08)
        shared_->r4_h2p.ready.store(0, std::memory_order_release);

    pending_clear_mask_ = 0;
    update_pnmi();
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void TubeParasitePort::reset()
{
    // Clear P-to-H registers (parasite-owned).
    shared_->r1_p2h.head.store(0, std::memory_order_relaxed);
    shared_->r1_p2h.tail.store(0, std::memory_order_relaxed);
    shared_->r1_p2h.count.store(0, std::memory_order_relaxed);

    shared_->r2_p2h.value.store(0, std::memory_order_relaxed);
    shared_->r2_p2h.ready.store(0, std::memory_order_relaxed);

    // R3 P-to-H: initialise with dummy byte to prevent spurious PNMI.
    shared_->r3_p2h.data[0].store(0, std::memory_order_relaxed);
    shared_->r3_p2h.data[1].store(0, std::memory_order_relaxed);
    shared_->r3_p2h.head.store(0, std::memory_order_relaxed);
    shared_->r3_p2h.tail.store(1, std::memory_order_relaxed);
    shared_->r3_p2h.count.store(1, std::memory_order_relaxed);

    shared_->r4_p2h.value.store(0, std::memory_order_relaxed);
    shared_->r4_p2h.ready.store(0, std::memory_order_relaxed);

    // Clear H-to-P registers. During reset the host is expected to be
    // resetting its side too, so this is safe.
    shared_->r1_h2p.value.store(0, std::memory_order_relaxed);
    shared_->r1_h2p.ready.store(0, std::memory_order_relaxed);

    shared_->r2_h2p.value.store(0, std::memory_order_relaxed);
    shared_->r2_h2p.ready.store(0, std::memory_order_relaxed);

    shared_->r3_h2p.data[0].store(0, std::memory_order_relaxed);
    shared_->r3_h2p.data[1].store(0, std::memory_order_relaxed);
    shared_->r3_h2p.head.store(0, std::memory_order_relaxed);
    shared_->r3_h2p.tail.store(0, std::memory_order_relaxed);
    shared_->r3_h2p.count.store(0, std::memory_order_relaxed);

    shared_->r4_h2p.value.store(0, std::memory_order_relaxed);
    shared_->r4_h2p.ready.store(0, std::memory_order_relaxed);

    // Clear interrupt and deferred state.
    prev_pnmi_ = false;
    pnmi_edge_ = false;
    pending_clear_mask_ = 0;
    cached_r1_ = cached_r2_ = cached_r3_ = cached_r4_ = 0;

    // Release fence so all relaxed stores are visible.
    std::atomic_thread_fence(std::memory_order_release);
}

// ---------------------------------------------------------------------------
// FIFO/register helpers
// ---------------------------------------------------------------------------

void TubeParasitePort::enqueue_r1_p2h(uint8_t value)
{
    uint8_t count = shared_->r1_p2h.count.load(std::memory_order_acquire);
    if (count >= 24)
        return;

    uint8_t tail = shared_->r1_p2h.tail.load(std::memory_order_relaxed);
    shared_->r1_p2h.data[tail].store(value, std::memory_order_relaxed);
    shared_->r1_p2h.tail.store((tail + 1) % 24, std::memory_order_relaxed);
    shared_->r1_p2h.count.fetch_add(1, std::memory_order_release);
}

uint8_t TubeParasitePort::dequeue_r3_h2p()
{
    uint8_t count = shared_->r3_h2p.count.load(std::memory_order_acquire);
    if (count == 0)
        return shared_->host_data_latch.load(std::memory_order_relaxed);

    uint8_t head = shared_->r3_h2p.head.load(std::memory_order_relaxed);
    uint8_t value = shared_->r3_h2p.data[head].load(std::memory_order_acquire);
    shared_->r3_h2p.head.store(head ^ 1, std::memory_order_relaxed);
    shared_->r3_h2p.count.fetch_sub(1, std::memory_order_release);

    return value;
}

void TubeParasitePort::enqueue_r3_p2h(uint8_t value)
{
    uint8_t count = shared_->r3_p2h.count.load(std::memory_order_acquire);
    if (count >= 2)
        return;

    uint8_t tail = shared_->r3_p2h.tail.load(std::memory_order_relaxed);
    shared_->r3_p2h.data[tail].store(value, std::memory_order_relaxed);
    shared_->r3_p2h.tail.store(tail ^ 1, std::memory_order_relaxed);
    shared_->r3_p2h.count.fetch_add(1, std::memory_order_release);
}

}  // namespace beebium
