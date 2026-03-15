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

#include <beebium/tube/TubeHostPort.hpp>

namespace beebium {

// ---------------------------------------------------------------------------
// Host-side register read
// ---------------------------------------------------------------------------

uint8_t TubeHostPort::host_read(uint8_t offset)
{
    uint8_t result = 0;
    auto flags = shared_->control_flags.load(std::memory_order_acquire);

    switch (offset & 7) {
    case 0: {
        // R1 status + control flags.
        // Bit 7: R1 P-to-H data available (host can read).
        // Bit 6: R1 H-to-P not full (host can write).
        // Bits 5-0: control flags (P, V, M, J, I, Q).
        result = flags & 0x3F;
        if (shared_->r1_p2h.count.load(std::memory_order_acquire) > 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r1_h2p.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from P-to-H 24-byte FIFO.
        uint8_t pre_count = shared_->r1_p2h.count.load(std::memory_order_acquire);
        result = dequeue_r1_p2h();
        if (pre_count > 0)
            shared_->counters.r1_p2h_reads.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 2: {
        // R2 status.
        // Bit 7: R2 P-to-H data available.
        // Bit 6: R2 H-to-P not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (shared_->r2_p2h.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r2_h2p.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from P-to-H latch.
        result = shared_->r2_p2h.value.load(std::memory_order_acquire);
        if (shared_->r2_p2h.ready.exchange(0, std::memory_order_acq_rel) != 0) {
            shared_->counters.r2_p2h_reads.fetch_add(1, std::memory_order_relaxed);
        }
        break;
    }

    case 4: {
        // R3 status (host perspective).
        // Bit 7: R3 P-to-H data available (count >= threshold).
        // Bit 6: R3 H-to-P space available (count < threshold).
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;
        if (shared_->r3_p2h.count.load(std::memory_order_acquire) >= threshold)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r3_h2p.count.load(std::memory_order_acquire) < threshold)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 5: {
        // R3 data: read from P-to-H register.
        uint8_t pre_count = shared_->r3_p2h.count.load(std::memory_order_acquire);
        result = dequeue_r3_p2h();
        if (pre_count > 0)
            shared_->counters.r3_p2h_reads.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 6: {
        // R4 status.
        // Bit 7: R4 P-to-H data available.
        // Bit 6: R4 H-to-P not full.
        // Bits 5-0: read as 1 (App Note 004, note 11).
        result = 0x3F;
        if (shared_->r4_p2h.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r4_h2p.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from P-to-H latch.
        result = shared_->r4_p2h.value.load(std::memory_order_acquire);
        if (shared_->r4_p2h.ready.exchange(0, std::memory_order_acq_rel) != 0) {
            shared_->counters.r4_p2h_reads.fetch_add(1, std::memory_order_relaxed);
        }
        break;
    }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Host-side side-effect-free register read (debugger inspection)
// ---------------------------------------------------------------------------

uint8_t TubeHostPort::host_peek(uint8_t offset) const
{
    uint8_t result = 0;
    auto flags = shared_->control_flags.load(std::memory_order_acquire);

    switch (offset & 7) {
    case 0: {
        result = flags & 0x3F;
        if (shared_->r1_p2h.count.load(std::memory_order_acquire) > 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r1_h2p.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 1: {
        uint8_t count = shared_->r1_p2h.count.load(std::memory_order_acquire);
        if (count > 0) {
            uint8_t head = shared_->r1_p2h.head.load(std::memory_order_relaxed);
            result = shared_->r1_p2h.data[head].load(std::memory_order_relaxed);
        }
        break;
    }
    case 2: {
        result = 0x3F;
        if (shared_->r2_p2h.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r2_h2p.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = shared_->r2_p2h.value.load(std::memory_order_acquire);
        break;
    case 4: {
        result = 0x3F;
        uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;
        if (shared_->r3_p2h.count.load(std::memory_order_acquire) >= threshold)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r3_h2p.count.load(std::memory_order_acquire) < threshold)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 5: {
        uint8_t count = shared_->r3_p2h.count.load(std::memory_order_acquire);
        if (count > 0) {
            uint8_t head = shared_->r3_p2h.head.load(std::memory_order_relaxed);
            result = shared_->r3_p2h.data[head].load(std::memory_order_acquire);
        }
        break;
    }
    case 6: {
        result = 0x3F;
        if (shared_->r4_p2h.ready.load(std::memory_order_acquire) != 0)
            result |= TubeUla::DATA_AVAILABLE;
        if (shared_->r4_h2p.ready.load(std::memory_order_acquire) == 0)
            result |= TubeUla::SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = shared_->r4_p2h.value.load(std::memory_order_acquire);
        break;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Host-side register write
// ---------------------------------------------------------------------------

void TubeHostPort::host_write(uint8_t offset, uint8_t value)
{
    switch (offset & 7) {
    case 0: {
        // Control flag register.
        auto flags = shared_->control_flags.load(std::memory_order_acquire);
        if (value & TubeUla::FLAG_S) {
            // Set mode: OR specified bits into control flags.
            // T flag (bit 6) triggers soft reset -- it is an action, not stored.
            if (value & TubeUla::FLAG_T)
                soft_reset();
            flags |= (value & 0x3F);
        } else {
            // Clear mode: AND out specified bits.
            flags &= ~(value & 0x3F);
        }
        shared_->control_flags.store(flags, std::memory_order_release);
        break;
    }

    case 1: {
        // R1 data: write to H-to-P latch.
        // Bus stretching: spin until the parasite has consumed any previous byte.
        while (shared_->r1_h2p.ready.load(std::memory_order_acquire) != 0)
            ;
        shared_->r1_h2p.value.store(value, std::memory_order_relaxed);
        shared_->r1_h2p.ready.store(1, std::memory_order_release);
        shared_->counters.r1_h2p_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 2:
        // R2 status: no writable effect from host.
        break;

    case 3: {
        // R2 data: write to H-to-P latch.
        shared_->r2_h2p.value.store(value, std::memory_order_relaxed);
        shared_->r2_h2p.ready.store(1, std::memory_order_release);
        shared_->counters.r2_h2p_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 4:
        // R3 status: no writable effect from host.
        break;

    case 5: {
        // R3 data: write to H-to-P circular buffer.
        // Bus stretching: spin until the register has space.
        while (shared_->r3_h2p.count.load(std::memory_order_acquire) >= 2)
            ;
        uint8_t tail = shared_->r3_h2p.tail.load(std::memory_order_relaxed);
        shared_->r3_h2p.data[tail].store(value, std::memory_order_relaxed);
        shared_->r3_h2p.tail.store(tail ^ 1, std::memory_order_relaxed);
        shared_->r3_h2p.count.fetch_add(1, std::memory_order_release);
        shared_->counters.r3_h2p_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    case 6:
        // R4 status: no writable effect from host.
        break;

    case 7: {
        // R4 data: write to H-to-P latch.
        // Bus stretching: spin until the parasite has consumed any previous byte.
        while (shared_->r4_h2p.ready.load(std::memory_order_acquire) != 0)
            ;
        shared_->r4_h2p.value.store(value, std::memory_order_relaxed);
        shared_->r4_h2p.ready.store(1, std::memory_order_release);
        shared_->counters.r4_h2p_writes.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// HIRQ computation
// ---------------------------------------------------------------------------

bool TubeHostPort::hirq() const
{
    // HIRQ: Q=1 AND R4 P-to-H data available.
    auto flags = shared_->control_flags.load(std::memory_order_acquire);
    bool q = (flags & TubeUla::FLAG_Q) != 0;
    bool r4_data = shared_->r4_p2h.ready.load(std::memory_order_acquire) != 0;
    return q && r4_data;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void TubeHostPort::reset()
{
    shared_->control_flags.store(0, std::memory_order_relaxed);
    soft_reset();

    // Signal the parasite to reset via the lifecycle mailbox.
    shared_->host_command.store(
        static_cast<uint8_t>(TubeLifecycleCommand::Reset),
        std::memory_order_release);
}

void TubeHostPort::soft_reset()
{
    // Clear H-to-P registers (host-owned).
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

    // Clear P-to-H registers. Normally these are parasite-owned, but during
    // reset the parasite is expected to be stopped or resetting itself.
    shared_->r1_p2h.head.store(0, std::memory_order_relaxed);
    shared_->r1_p2h.tail.store(0, std::memory_order_relaxed);
    shared_->r1_p2h.count.store(0, std::memory_order_relaxed);

    shared_->r2_p2h.value.store(0, std::memory_order_relaxed);
    shared_->r2_p2h.ready.store(0, std::memory_order_relaxed);

    // R3 P-to-H: initialise with dummy byte to prevent spurious PNMI
    // on the parasite side (matches TubeUla::soft_reset behaviour).
    shared_->r3_p2h.data[0].store(0, std::memory_order_relaxed);
    shared_->r3_p2h.data[1].store(0, std::memory_order_relaxed);
    shared_->r3_p2h.head.store(0, std::memory_order_relaxed);
    shared_->r3_p2h.tail.store(1, std::memory_order_relaxed);
    shared_->r3_p2h.count.store(1, std::memory_order_relaxed);

    shared_->r4_p2h.value.store(0, std::memory_order_relaxed);
    shared_->r4_p2h.ready.store(0, std::memory_order_relaxed);

    // Release fence so all relaxed stores are visible to the parasite.
    std::atomic_thread_fence(std::memory_order_release);
}

// ---------------------------------------------------------------------------
// FIFO helpers
// ---------------------------------------------------------------------------

uint8_t TubeHostPort::dequeue_r1_p2h()
{
    uint8_t count = shared_->r1_p2h.count.load(std::memory_order_acquire);
    if (count == 0)
        return 0;

    uint8_t head = shared_->r1_p2h.head.load(std::memory_order_relaxed);
    uint8_t value = shared_->r1_p2h.data[head].load(std::memory_order_relaxed);
    shared_->r1_p2h.head.store((head + 1) % 24, std::memory_order_relaxed);
    shared_->r1_p2h.count.fetch_sub(1, std::memory_order_release);

    return value;
}

uint8_t TubeHostPort::dequeue_r3_p2h()
{
    uint8_t count = shared_->r3_p2h.count.load(std::memory_order_acquire);
    if (count == 0)
        return 0;

    uint8_t head = shared_->r3_p2h.head.load(std::memory_order_relaxed);
    uint8_t value = shared_->r3_p2h.data[head].load(std::memory_order_acquire);
    shared_->r3_p2h.head.store(head ^ 1, std::memory_order_relaxed);
    shared_->r3_p2h.count.fetch_sub(1, std::memory_order_release);

    return value;
}

}  // namespace beebium
