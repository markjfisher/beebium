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
#include "TubeParasiteBackend.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace beebium {

// Tube ULA register model.
//
// Models the Acorn Tube ULA's four register sets, control flags, and
// interrupt outputs. This is the authoritative FIFO model used for both
// in-process testing and as the register bridge in the extension-based
// coprocessor architecture.
//
// Thread safety: lock-free. All register fields are atomic with
// acquire/release ordering. Each register direction follows the SPSC
// (single-producer, single-consumer) pattern:
//   - H-to-P: host writes (producer), parasite reads (consumer)
//   - P-to-H: parasite writes (producer), host reads (consumer)
// No mutex is needed. Bus stretching uses atomic spin-waits.
//
// The host and parasite sides see different views of the same registers:
// different status bits, different read/write targets. The caller selects
// the perspective by calling host_read/host_write or parasite_read/
// parasite_write.

class TubeUla : public TubeHostBackend, public TubeParasiteBackend {
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
    uint8_t host_peek(uint8_t offset) const override;
    void host_write(uint8_t offset, uint8_t value) override;

    // Parasite-side register access (offsets 0-7, mirrored from &FEF8-&FEFF).
    uint8_t parasite_read(uint8_t offset) override;
    uint8_t parasite_peek(uint8_t offset) const override;
    void parasite_write(uint8_t offset, uint8_t value) override;

    // Interrupt outputs (active high in this model; caller inverts if needed).
    // These use the Tube-specific names from Application Note 004:
    //   hirq -- Host IRQ (active when Q=1 and R4 has P-to-H data)
    //   pirq -- Parasite IRQ (active when I=1 and R1 has data, or J=1 and R4 has data)
    //   pnmi -- Parasite NMI (edge-triggered from R3 activity when M=1)
    //   pnmi_level -- raw combinational PNMI output (for M6502 edge detection)
    // TubeSocket adapts hirq() to the generic IrqSource::irq_pending() interface.
    bool hirq() const override;
    bool pirq() const override;
    bool pnmi() const;
    bool pnmi_level() const override;

    // Read control flags (bits 0-5: Q, I, J, M, V, P).
    uint8_t control_flags() const {
        return control_flags_.load(std::memory_order_acquire);
    }

    // Test whether the parasite reset line is currently asserted.
    bool parasite_reset_active() const {
        return (control_flags_.load(std::memory_order_acquire) & FLAG_P) != 0;
    }

    // Bus stretching: host_write spin-waits when a register is full, so
    // stretched() always returns false (the write completes before returning).
    bool stretched() const override { return false; }

    // Access the NMI edge detector state (parasite-local).
    bool prev_pnmi() const { return prev_pnmi_; }

private:
    // Soft reset (T flag) -- clears all register data but preserves control flags.
    void soft_reset();

    // Recompute interrupt outputs.
    // Split by thread ownership to avoid data races on edge detection state.
    void update_host_interrupts();     // HIRQ only (called from host thread)
    void update_parasite_interrupts(); // PIRQ + PNMI (called from parasite thread)

    // Control flag register (bits 0-5: Q, I, J, M, V, P).
    // Single writer (host thread).
    std::atomic<uint8_t> control_flags_{0};

    // Atomic latch: 1-byte data with ready/full flags.
    // Used for R1 H-to-P, R2 both directions, R4 both directions.
    // Producer stores data (relaxed) then sets ready+full (release).
    // Consumer loads ready (acquire), loads data (relaxed), clears (release).
    struct AtomicLatch {
        std::atomic<uint8_t> data{0};
        std::atomic<bool> available{false};  // data waiting for consumer
        std::atomic<bool> full{false};       // producer cannot write
    };

    // Atomic SPSC ring buffer (24 bytes, for R1 P-to-H).
    struct AtomicFifo24 {
        std::array<std::atomic<uint8_t>, 24> data{};
        std::atomic<uint8_t> head{0};   // consumer reads here
        std::atomic<uint8_t> tail{0};   // producer writes here
        std::atomic<uint8_t> count{0};  // modified by both via fetch_add/fetch_sub
    };

    // Atomic 2-slot register (for R3, each direction).
    // Count and pending flag packed into atomic<uint16_t> -- see TubeShared.hpp
    // TubeReg3 for the pack/unpack helpers (not reused here to avoid the
    // dependency, but the same bit layout applies).
    struct AtomicReg3 {
        std::array<std::atomic<uint8_t>, 2> data{};
        std::atomic<uint8_t> head{0};
        std::atomic<uint8_t> tail{0};
        // Packed: low byte = count (0-2), high byte = pending flag (0 or 1).
        std::atomic<uint16_t> state{0};

        static constexpr uint16_t pack(uint8_t count, bool pending) {
            return static_cast<uint16_t>(count)
                 | (static_cast<uint16_t>(pending ? 1 : 0) << 8);
        }
        static constexpr uint8_t count_of(uint16_t s) {
            return static_cast<uint8_t>(s & 0xFF);
        }
        static constexpr bool pending_of(uint16_t s) {
            return (s >> 8) != 0;
        }
    };

    // Register 1: H-to-P is a 1-byte latch; P-to-H is a 24-byte FIFO.
    AtomicLatch r1_h2p_;
    AtomicFifo24 r1_p2h_;

    // Register 2: 1-byte latch in each direction.
    AtomicLatch r2_h2p_;
    AtomicLatch r2_p2h_;

    // Register 3: 2-byte FIFO in each direction.
    AtomicReg3 r3_h2p_;
    AtomicReg3 r3_p2h_;

    // Register 4: 1-byte latch in each direction.
    AtomicLatch r4_h2p_;
    AtomicLatch r4_p2h_;

    // Interrupt output state (lock-free, updated by update_interrupts()).
    std::atomic<bool> hirq_{false};
    std::atomic<bool> pirq_{false};
    std::atomic<bool> pnmi_level_{false};
    bool prev_pnmi_ = false;   // PNMI edge detector (parasite thread only)
    std::atomic<bool> pnmi_edge_{false};
};

}  // namespace beebium
