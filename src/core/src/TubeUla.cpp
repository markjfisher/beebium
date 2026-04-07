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

#include <beebium/tube/TubeUla.hpp>

#include <thread>

namespace beebium {

TubeUla::TubeUla()
{
    reset();
}

void TubeUla::reset()
{
    control_flags_.store(0, std::memory_order_relaxed);
    soft_reset();
}

void TubeUla::soft_reset()
{
    // R1: clear latch and FIFO.
    r1_h2p_.data.store(0, std::memory_order_relaxed);
    r1_h2p_.available.store(false, std::memory_order_relaxed);
    r1_h2p_.full.store(false, std::memory_order_relaxed);
    for (auto& b : r1_p2h_.data) b.store(0, std::memory_order_relaxed);
    r1_p2h_.head.store(0, std::memory_order_relaxed);
    r1_p2h_.tail.store(0, std::memory_order_relaxed);
    r1_p2h_.count.store(0, std::memory_order_relaxed);

    // R2: clear latches.
    r2_h2p_.data.store(0, std::memory_order_relaxed);
    r2_h2p_.available.store(false, std::memory_order_relaxed);
    r2_h2p_.full.store(false, std::memory_order_relaxed);
    r2_p2h_.data.store(0, std::memory_order_relaxed);
    r2_p2h_.available.store(false, std::memory_order_relaxed);
    r2_p2h_.full.store(false, std::memory_order_relaxed);

    // R3: clear FIFOs. P-to-H gets one dummy byte to prevent spurious PNMI.
    r3_h2p_.data[0].store(0, std::memory_order_relaxed);
    r3_h2p_.data[1].store(0, std::memory_order_relaxed);
    r3_h2p_.head.store(0, std::memory_order_relaxed);
    r3_h2p_.tail.store(0, std::memory_order_relaxed);
    r3_h2p_.state.store(AtomicReg3::pack(0, false), std::memory_order_relaxed);
    r3_p2h_.data[0].store(0, std::memory_order_relaxed);
    r3_p2h_.data[1].store(0, std::memory_order_relaxed);
    r3_p2h_.head.store(0, std::memory_order_relaxed);
    r3_p2h_.tail.store(1, std::memory_order_relaxed);
    r3_p2h_.state.store(AtomicReg3::pack(1, true), std::memory_order_relaxed);

    // R4: clear latches.
    r4_h2p_.data.store(0, std::memory_order_relaxed);
    r4_h2p_.available.store(false, std::memory_order_relaxed);
    r4_h2p_.full.store(false, std::memory_order_relaxed);
    r4_p2h_.data.store(0, std::memory_order_relaxed);
    r4_p2h_.available.store(false, std::memory_order_relaxed);
    r4_p2h_.full.store(false, std::memory_order_relaxed);

    // Interrupt state.
    hirq_.store(false, std::memory_order_relaxed);
    pirq_.store(false, std::memory_order_relaxed);
    pnmi_level_.store(false, std::memory_order_relaxed);
    prev_pnmi_.store(false, std::memory_order_relaxed);
    pnmi_edge_.store(false, std::memory_order_relaxed);

    // Release fence so all relaxed stores are visible.
    std::atomic_thread_fence(std::memory_order_release);

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Host-side register access
// ---------------------------------------------------------------------------

uint8_t TubeUla::host_read(uint8_t offset)
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_.load(std::memory_order_acquire) & 0x3F;
        if (r1_p2h_.count.load(std::memory_order_acquire) > 0)
            result |= DATA_AVAILABLE;
        if (!r1_h2p_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from P-to-H 24-byte FIFO.
        uint8_t count = r1_p2h_.count.load(std::memory_order_acquire);
        if (count > 0) {
            uint8_t head = r1_p2h_.head.load(std::memory_order_relaxed);
            result = r1_p2h_.data[head].load(std::memory_order_relaxed);
            r1_p2h_.head.store((head + 1) % 24, std::memory_order_relaxed);
            r1_p2h_.count.fetch_sub(1, std::memory_order_release);
        }
        break;
    }

    case 2: {
        result = 0x3F;
        if (r2_p2h_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r2_h2p_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from P-to-H latch.
        auto was_available = r2_p2h_.available.load(std::memory_order_acquire);
        result = r2_p2h_.data.load(std::memory_order_relaxed);
        if (was_available) {
            r2_p2h_.available.store(false, std::memory_order_relaxed);
            r2_p2h_.full.store(false, std::memory_order_release);
        }
        break;
    }

    case 4: {
        result = 0x3F;
        auto flags = control_flags_.load(std::memory_order_acquire);
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        auto p2h_state = r3_p2h_.state.load(std::memory_order_acquire);
        auto h2p_state = r3_h2p_.state.load(std::memory_order_acquire);
        if (AtomicReg3::pending_of(p2h_state) || AtomicReg3::count_of(p2h_state) >= threshold)
            result |= DATA_AVAILABLE;
        if (!AtomicReg3::pending_of(h2p_state))
            result |= SPACE_AVAILABLE;
        break;
    }

    case 5: {
        // R3 data: read from P-to-H register.
        uint16_t old_state = r3_p2h_.state.load(std::memory_order_acquire);
        uint8_t count = AtomicReg3::count_of(old_state);
        if (count > 0) {
            uint8_t head = r3_p2h_.head.load(std::memory_order_relaxed);
            result = r3_p2h_.data[head].load(std::memory_order_acquire);
            r3_p2h_.head.store(head ^ 1, std::memory_order_relaxed);
            // CAS to decrement count and clear pending if empty.
            uint16_t new_state;
            do {
                count = AtomicReg3::count_of(old_state);
                uint8_t new_count = count - 1;
                bool pending = (new_count == 0) ? false : AtomicReg3::pending_of(old_state);
                new_state = AtomicReg3::pack(new_count, pending);
            } while (!r3_p2h_.state.compare_exchange_weak(
                old_state, new_state, std::memory_order_release, std::memory_order_acquire));
        }
        break;
    }

    case 6: {
        result = 0x3F;
        if (r4_p2h_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r4_h2p_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from P-to-H latch.
        auto was_available = r4_p2h_.available.load(std::memory_order_acquire);
        result = r4_p2h_.data.load(std::memory_order_relaxed);
        if (was_available) {
            r4_p2h_.available.store(false, std::memory_order_relaxed);
            r4_p2h_.full.store(false, std::memory_order_release);
        }
        break;
    }
    }

    update_interrupts();
    return result;
}

uint8_t TubeUla::host_peek(uint8_t offset) const
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_.load(std::memory_order_acquire) & 0x3F;
        if (r1_p2h_.count.load(std::memory_order_acquire) > 0)
            result |= DATA_AVAILABLE;
        if (!r1_h2p_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }
    case 1: {
        uint8_t count = r1_p2h_.count.load(std::memory_order_acquire);
        result = (count > 0)
            ? r1_p2h_.data[r1_p2h_.head.load(std::memory_order_relaxed)].load(std::memory_order_relaxed)
            : 0;
        break;
    }
    case 2: {
        result = 0x3F;
        if (r2_p2h_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r2_h2p_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = r2_p2h_.data.load(std::memory_order_acquire);
        break;
    case 4: {
        result = 0x3F;
        auto flags_v = control_flags_.load(std::memory_order_acquire);
        uint8_t thresh = (flags_v & FLAG_V) ? 2 : 1;
        auto p2h_st = r3_p2h_.state.load(std::memory_order_acquire);
        if (AtomicReg3::pending_of(p2h_st) || AtomicReg3::count_of(p2h_st) >= thresh)
            result |= DATA_AVAILABLE;
        if (!AtomicReg3::pending_of(r3_h2p_.state.load(std::memory_order_acquire)))
            result |= SPACE_AVAILABLE;
        break;
    }
    case 5: {
        uint8_t count = AtomicReg3::count_of(r3_p2h_.state.load(std::memory_order_acquire));
        if (count > 0) {
            uint8_t head = r3_p2h_.head.load(std::memory_order_relaxed);
            result = r3_p2h_.data[head].load(std::memory_order_acquire);
        }
        break;
    }
    case 6: {
        result = 0x3F;
        if (r4_p2h_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r4_h2p_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = r4_p2h_.data.load(std::memory_order_acquire);
        break;
    }

    return result;
}

void TubeUla::host_write(uint8_t offset, uint8_t value)
{
    switch (offset & 7) {
    case 0: {
        // Control flag register.
        auto flags = control_flags_.load(std::memory_order_acquire);
        if (value & FLAG_S) {
            if (value & FLAG_T)
                soft_reset();
            flags |= (value & 0x3F);
        } else {
            flags &= ~(value & 0x3F);
        }
        control_flags_.store(flags, std::memory_order_release);
        break;
    }

    case 1: {
        // R1 data: write to H-to-P latch.
        // Bus stretching: spin-wait on atomic full flag.
        while (r1_h2p_.full.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        r1_h2p_.data.store(value, std::memory_order_relaxed);
        r1_h2p_.available.store(true, std::memory_order_relaxed);
        r1_h2p_.full.store(true, std::memory_order_release);
        break;
    }

    case 2:
        break;

    case 3: {
        // R2 data: write to H-to-P latch. No bus stretching.
        r2_h2p_.data.store(value, std::memory_order_relaxed);
        r2_h2p_.available.store(true, std::memory_order_relaxed);
        r2_h2p_.full.store(true, std::memory_order_release);
        break;
    }

    case 4:
        break;

    case 5: {
        // R3 data: write to H-to-P register.
        // Bus stretching: spin-wait until count < 2.
        while (AtomicReg3::count_of(r3_h2p_.state.load(std::memory_order_acquire)) >= 2) {
            std::this_thread::yield();
        }
        uint8_t tail = r3_h2p_.tail.load(std::memory_order_relaxed);
        r3_h2p_.data[tail].store(value, std::memory_order_relaxed);
        r3_h2p_.tail.store(tail ^ 1, std::memory_order_relaxed);
        // CAS to increment count and set pending if threshold reached.
        auto flags = control_flags_.load(std::memory_order_acquire);
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        uint16_t old_state = r3_h2p_.state.load(std::memory_order_acquire);
        uint16_t new_state;
        do {
            uint8_t new_count = AtomicReg3::count_of(old_state) + 1;
            bool pending = AtomicReg3::pending_of(old_state) || (new_count >= threshold);
            new_state = AtomicReg3::pack(new_count, pending);
        } while (!r3_h2p_.state.compare_exchange_weak(
            old_state, new_state, std::memory_order_release, std::memory_order_acquire));
        break;
    }

    case 6:
        break;

    case 7: {
        // R4 data: write to H-to-P latch. Bus stretching.
        while (r4_h2p_.full.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        r4_h2p_.data.store(value, std::memory_order_relaxed);
        r4_h2p_.available.store(true, std::memory_order_relaxed);
        r4_h2p_.full.store(true, std::memory_order_release);
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
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_.load(std::memory_order_acquire) & 0x3F;
        if (r1_h2p_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (r1_p2h_.count.load(std::memory_order_acquire) < 24)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from H-to-P latch.
        auto was_available = r1_h2p_.available.load(std::memory_order_acquire);
        result = r1_h2p_.data.load(std::memory_order_relaxed);
        if (was_available) {
            r1_h2p_.available.store(false, std::memory_order_relaxed);
            r1_h2p_.full.store(false, std::memory_order_release);
        }
        break;
    }

    case 2: {
        result = 0x3F;
        if (r2_h2p_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r2_p2h_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from H-to-P latch.
        auto was_available = r2_h2p_.available.load(std::memory_order_acquire);
        result = r2_h2p_.data.load(std::memory_order_relaxed);
        if (was_available) {
            r2_h2p_.available.store(false, std::memory_order_relaxed);
            r2_h2p_.full.store(false, std::memory_order_release);
        }
        break;
    }

    case 4: {
        result = 0x1F;
        auto flags_v = control_flags_.load(std::memory_order_acquire);
        uint8_t threshold = (flags_v & FLAG_V) ? 2 : 1;
        auto h2p_st = r3_h2p_.state.load(std::memory_order_acquire);
        auto p2h_st = r3_p2h_.state.load(std::memory_order_acquire);
        bool h2p_data = AtomicReg3::pending_of(h2p_st) || AtomicReg3::count_of(h2p_st) >= threshold;
        bool p2h_space = !AtomicReg3::pending_of(p2h_st);
        if (h2p_data)
            result |= DATA_AVAILABLE;
        if (p2h_space)
            result |= SPACE_AVAILABLE;
        if (h2p_data || p2h_space)
            result |= 0x20;  // N flag
        break;
    }

    case 5: {
        // R3 data: read from H-to-P register.
        uint16_t old_state = r3_h2p_.state.load(std::memory_order_acquire);
        uint8_t count = AtomicReg3::count_of(old_state);
        if (count > 0) {
            uint8_t head = r3_h2p_.head.load(std::memory_order_relaxed);
            result = r3_h2p_.data[head].load(std::memory_order_acquire);
            r3_h2p_.head.store(head ^ 1, std::memory_order_relaxed);
            uint16_t new_state;
            do {
                count = AtomicReg3::count_of(old_state);
                uint8_t new_count = count - 1;
                bool pending = (new_count == 0) ? false : AtomicReg3::pending_of(old_state);
                new_state = AtomicReg3::pack(new_count, pending);
            } while (!r3_h2p_.state.compare_exchange_weak(
                old_state, new_state, std::memory_order_release, std::memory_order_acquire));
        }
        break;
    }

    case 6: {
        result = 0x3F;
        if (r4_h2p_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r4_p2h_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from H-to-P latch.
        auto was_available = r4_h2p_.available.load(std::memory_order_acquire);
        result = r4_h2p_.data.load(std::memory_order_relaxed);
        if (was_available) {
            r4_h2p_.available.store(false, std::memory_order_relaxed);
            r4_h2p_.full.store(false, std::memory_order_release);
        }
        break;
    }
    }

    update_interrupts();
    return result;
}

uint8_t TubeUla::parasite_peek(uint8_t offset) const
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_.load(std::memory_order_acquire) & 0x3F;
        if (r1_h2p_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (r1_p2h_.count.load(std::memory_order_acquire) < 24)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 1:
        result = r1_h2p_.data.load(std::memory_order_acquire);
        break;
    case 2: {
        result = 0x3F;
        if (r2_h2p_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r2_p2h_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = r2_h2p_.data.load(std::memory_order_acquire);
        break;
    case 4: {
        result = 0x1F;
        auto flags_v = control_flags_.load(std::memory_order_acquire);
        uint8_t threshold = (flags_v & FLAG_V) ? 2 : 1;
        auto h2p_st = r3_h2p_.state.load(std::memory_order_acquire);
        auto p2h_st = r3_p2h_.state.load(std::memory_order_acquire);
        bool h2p_data = AtomicReg3::pending_of(h2p_st) || AtomicReg3::count_of(h2p_st) >= threshold;
        bool p2h_space = !AtomicReg3::pending_of(p2h_st);
        if (h2p_data)
            result |= DATA_AVAILABLE;
        if (p2h_space)
            result |= SPACE_AVAILABLE;
        if (h2p_data || p2h_space)
            result |= 0x20;
        break;
    }
    case 5: {
        uint8_t count = AtomicReg3::count_of(r3_h2p_.state.load(std::memory_order_acquire));
        if (count > 0) {
            uint8_t head = r3_h2p_.head.load(std::memory_order_relaxed);
            result = r3_h2p_.data[head].load(std::memory_order_acquire);
        }
        break;
    }
    case 6: {
        result = 0x3F;
        if (r4_h2p_.available.load(std::memory_order_acquire))
            result |= DATA_AVAILABLE;
        if (!r4_p2h_.full.load(std::memory_order_acquire))
            result |= SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = r4_h2p_.data.load(std::memory_order_acquire);
        break;
    }

    return result;
}

void TubeUla::parasite_write(uint8_t offset, uint8_t value)
{
    switch (offset & 7) {
    case 0:
        break;

    case 1: {
        // R1 data: write to P-to-H 24-byte FIFO.
        uint8_t count = r1_p2h_.count.load(std::memory_order_acquire);
        if (count < 24) {
            uint8_t tail = r1_p2h_.tail.load(std::memory_order_relaxed);
            r1_p2h_.data[tail].store(value, std::memory_order_relaxed);
            r1_p2h_.tail.store((tail + 1) % 24, std::memory_order_relaxed);
            r1_p2h_.count.fetch_add(1, std::memory_order_release);
        }
        break;
    }

    case 2:
        break;

    case 3: {
        // R2 data: write to P-to-H latch. No bus stretching.
        r2_p2h_.data.store(value, std::memory_order_relaxed);
        r2_p2h_.available.store(true, std::memory_order_relaxed);
        r2_p2h_.full.store(true, std::memory_order_release);
        break;
    }

    case 4:
        break;

    case 5: {
        // R3 data: write to P-to-H register.
        uint16_t old_state = r3_p2h_.state.load(std::memory_order_acquire);
        uint8_t count = AtomicReg3::count_of(old_state);
        if (count < 2) {
            uint8_t tail = r3_p2h_.tail.load(std::memory_order_relaxed);
            r3_p2h_.data[tail].store(value, std::memory_order_relaxed);
            r3_p2h_.tail.store(tail ^ 1, std::memory_order_relaxed);
            auto flags = control_flags_.load(std::memory_order_acquire);
            uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
            uint16_t new_state;
            do {
                uint8_t new_count = AtomicReg3::count_of(old_state) + 1;
                bool pending = AtomicReg3::pending_of(old_state) || (new_count >= threshold);
                new_state = AtomicReg3::pack(new_count, pending);
            } while (!r3_p2h_.state.compare_exchange_weak(
                old_state, new_state, std::memory_order_release, std::memory_order_acquire));
        }
        break;
    }

    case 6:
        break;

    case 7: {
        // R4 data: write to P-to-H latch.
        r4_p2h_.data.store(value, std::memory_order_relaxed);
        r4_p2h_.available.store(true, std::memory_order_relaxed);
        r4_p2h_.full.store(true, std::memory_order_release);
        break;
    }
    }

    update_interrupts();
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
    auto flags = control_flags_.load(std::memory_order_acquire);

    // HIRQ: Q=1 AND R4 P-to-H has data available.
    hirq_.store((flags & FLAG_Q)
             && r4_p2h_.available.load(std::memory_order_acquire),
                std::memory_order_release);

    // PIRQ: (I=1 AND R1 H-to-P has data) OR (J=1 AND R4 H-to-P has data).
    pirq_.store(((flags & FLAG_I)
              && r1_h2p_.available.load(std::memory_order_acquire))
             || ((flags & FLAG_J)
              && r4_h2p_.available.load(std::memory_order_acquire)),
                std::memory_order_release);

    // PNMI level computation.
    bool new_pnmi = false;
    if (flags & FLAG_M) {
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        auto h2p_st = r3_h2p_.state.load(std::memory_order_acquire);
        auto p2h_st = r3_p2h_.state.load(std::memory_order_acquire);
        bool h2p_data = AtomicReg3::pending_of(h2p_st)
                     || AtomicReg3::count_of(h2p_st) >= threshold;
        bool p2h_space = !AtomicReg3::pending_of(p2h_st);
        new_pnmi = h2p_data || p2h_space;
    }

    // Edge detection: fire on 0-to-1 transition.
    bool prev = prev_pnmi_.load(std::memory_order_acquire);
    if (new_pnmi && !prev)
        pnmi_edge_.store(true, std::memory_order_release);

    // Clear edge when the level drops (cause removed).
    if (!new_pnmi)
        pnmi_edge_.store(false, std::memory_order_release);

    prev_pnmi_.store(new_pnmi, std::memory_order_release);
    pnmi_level_.store(new_pnmi, std::memory_order_release);
}

}  // namespace beebium
