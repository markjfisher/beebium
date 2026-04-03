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

#include <array>
#include <atomic>
#include <cstdint>

namespace beebium {

// Shared memory layout for cross-process Tube ULA communication.
//
// This struct is placed in a shared memory region (shm_open/mmap) and accessed
// by both the host and parasite processes. All mutable fields are atomic with
// acquire/release semantics. The layout follows the single-writer principle:
// each field has exactly one writer process.
//
// Field ownership:
//   Host writes:     control_flags, r1_h2p, r2_h2p, r3_h2p, r4_h2p, host_command
//   Parasite writes: r1_p2h, r2_p2h, r3_p2h, r4_p2h, parasite_ack
//
// Cache line alignment prevents false sharing between host-written and
// parasite-written fields.

// Validation magic: ASCII "TUBE" = 0x54554245
static constexpr uint32_t TUBE_SHARED_MAGIC = 0x54554245;
static constexpr uint32_t TUBE_SHARED_VERSION = 1;

// Single-byte latch (R1 H-to-P, R2 both directions, R4 both directions).
// Writer stores value then sets ready; reader clears ready after consuming.
struct TubeLatch {
    std::atomic<uint8_t> value{0};
    std::atomic<uint8_t> ready{0};  // 0 = empty, 1 = data available
};

// 24-byte SPSC ring buffer (R1 P-to-H only).
// Parasite writes at tail; host reads from head.
struct TubeFifo24 {
    std::array<std::atomic<uint8_t>, 24> data{};
    std::atomic<uint8_t> head{0};
    std::atomic<uint8_t> tail{0};
    std::atomic<uint8_t> count{0};
};

// 2-slot circular buffer (R3 in each direction).
// Supports 1-byte or 2-byte transfer mode (selected by V flag).
// Producer writes at data[tail], consumer reads from data[head].
// Count uses fetch_add/fetch_sub for safe cross-process access.
// Data availability is derived from count >= threshold; there is no separate
// pending flag because it cannot be kept consistent with count across processes
// without an atomic coupling mechanism.
struct TubeReg3 {
    std::array<std::atomic<uint8_t>, 2> data{};
    std::atomic<uint8_t> head{0};     // consumer read index (0 or 1)
    std::atomic<uint8_t> tail{0};     // producer write index (0 or 1)
    std::atomic<uint8_t> count{0};    // shared; use fetch_add/fetch_sub
};

// Per-register transfer counters for diagnostics.
// Each counter tracks the total number of bytes written to or read from
// a register direction. The writer-side process increments its own counters
// (single-writer principle), and both sides can read all counters.
//
// Field ownership follows the register direction:
//   Host increments:     h2p_writes (R1-R4), p2h_reads (R1-R4)
//   Parasite increments: p2h_writes (R1-R4), h2p_reads (R1-R4)
struct TubeCounters {
    // Host-to-parasite direction
    std::atomic<uint64_t> r1_h2p_writes{0};  // host wrote to R1 H→P
    std::atomic<uint64_t> r1_h2p_reads{0};   // parasite read from R1 H→P
    std::atomic<uint64_t> r2_h2p_writes{0};
    std::atomic<uint64_t> r2_h2p_reads{0};
    std::atomic<uint64_t> r3_h2p_writes{0};
    std::atomic<uint64_t> r3_h2p_reads{0};
    std::atomic<uint64_t> r4_h2p_writes{0};
    std::atomic<uint64_t> r4_h2p_reads{0};

    // Parasite-to-host direction
    std::atomic<uint64_t> r1_p2h_writes{0};  // parasite wrote to R1 P→H
    std::atomic<uint64_t> r1_p2h_reads{0};   // host read from R1 P→H
    std::atomic<uint64_t> r2_p2h_writes{0};
    std::atomic<uint64_t> r2_p2h_reads{0};
    std::atomic<uint64_t> r3_p2h_writes{0};
    std::atomic<uint64_t> r3_p2h_reads{0};
    std::atomic<uint64_t> r4_p2h_writes{0};
    std::atomic<uint64_t> r4_p2h_reads{0};
};

// Lifecycle commands sent from host to parasite via the mailbox.
enum class TubeLifecycleCommand : uint8_t {
    None     = 0,
    Reset    = 1,   // Parasite should perform a hard reset
    Freeze   = 2,   // Parasite should stop and await further instructions
    Shutdown = 3,   // Parasite should terminate cleanly
};

// Lifecycle acknowledgements sent from parasite to host.
enum class TubeLifecycleAck : uint8_t {
    None      = 0,
    ResetDone = 1,
    Frozen    = 2,
    Exiting   = 3,
};

// The shared memory structure.
//
// Alignment to 64-byte cache line boundaries minimises false sharing.
// Host-written fields and parasite-written fields are on separate cache lines.
struct alignas(64) TubeShared {
    // --- Header (immutable after creation) ---
    uint32_t magic = TUBE_SHARED_MAGIC;
    uint32_t version = TUBE_SHARED_VERSION;

    // --- Control flags (host writes, both read) ---
    // Bits: Q(0), I(1), J(2), M(3), V(4), P(5)
    // Uses the same bit positions as TubeUla::FLAG_Q etc.
    alignas(64) std::atomic<uint8_t> control_flags{0};

    // --- H-to-P registers (host writes, parasite reads) ---
    TubeLatch r1_h2p;           // R1: 1-byte latch
    TubeLatch r2_h2p;           // R2: 1-byte latch
    TubeReg3  r3_h2p;           // R3: 2-byte register
    TubeLatch r4_h2p;           // R4: 1-byte latch

    // --- P-to-H registers (parasite writes, host reads) ---
    alignas(64) TubeFifo24 r1_p2h;  // R1: 24-byte FIFO
    TubeLatch r2_p2h;               // R2: 1-byte latch
    TubeReg3  r3_p2h;               // R3: 2-byte register
    TubeLatch r4_p2h;               // R4: 1-byte latch

    // --- Data bus latches ---
    // The real Tube ULA's data bus retains the last written value. Empty
    // register reads return this latch value rather than zero. Each side
    // has its own latch: host_data_latch for the last host write, and
    // parasite_data_latch for the last parasite write.
    // Reference: B-Em commit 97f0ad6, hoglet's hardware tests.
    alignas(64) std::atomic<uint8_t> host_data_latch{0};
    std::atomic<uint8_t> parasite_data_latch{0};

    // --- Transfer counters (both sides read, single-writer per counter) ---
    alignas(64) TubeCounters counters;

    // --- Bus stretch cancellation ---
    // Set by either side's debugger to break out of bus stretching spin loops.
    // Checked by both TubeHostPort and TubeParasitePort during writes that
    // spin-wait for the other side to consume data. Without this, pausing a
    // machine that is bus-stretched would hang indefinitely because the spin
    // loop never checks the pause flag.
    alignas(64) std::atomic<bool> bus_stretch_cancel{false};

    // --- Cross-processor debugger stop signal ---
    // Set by one processor's debugger when a breakpoint/watchpoint with
    // stop_counterpart fires. Checked in both tick loops so the other
    // processor stops within one cycle. Separate from bus_stretch_cancel
    // which is a bus-stretching concern, not a debugger concern.
    alignas(64) std::atomic<bool> debugger_stop_signal{false};

    // --- I/O pending flags ---
    // Set by one side after writing to a Tube register, to signal the other
    // side's PacingClock that it should skip sleeping and run another tick
    // immediately. Cleared by the receiving side's PacingClock at the start
    // of each tick. These flags reduce Tube R2 handshake latency from ~50ms
    // (one pacing tick per boundary crossing) to near-zero (back-to-back ticks).
    //
    // Host writes io_pending_parasite; parasite writes io_pending_host.
    alignas(64) std::atomic<bool> io_pending_host{false};
    std::atomic<bool> io_pending_parasite{false};

    // --- Lifecycle mailbox ---
    alignas(64) std::atomic<uint8_t> host_command{0};
    std::atomic<uint8_t> parasite_ack{0};

    // Validate that the shared memory region contains a valid TubeShared.
    bool valid() const {
        return magic == TUBE_SHARED_MAGIC && version == TUBE_SHARED_VERSION;
    }

    // Initialise all fields to their reset state.
    // Called by the host after creating the shared memory region.
    void init() {
        magic = TUBE_SHARED_MAGIC;
        version = TUBE_SHARED_VERSION;

        control_flags.store(0, std::memory_order_relaxed);

        r1_h2p.value.store(0, std::memory_order_relaxed);
        r1_h2p.ready.store(0, std::memory_order_relaxed);
        r2_h2p.value.store(0, std::memory_order_relaxed);
        r2_h2p.ready.store(0, std::memory_order_relaxed);
        r3_h2p.data[0].store(0, std::memory_order_relaxed);
        r3_h2p.data[1].store(0, std::memory_order_relaxed);
        r3_h2p.head.store(0, std::memory_order_relaxed);
        r3_h2p.tail.store(0, std::memory_order_relaxed);
        r3_h2p.count.store(0, std::memory_order_relaxed);
        r4_h2p.value.store(0, std::memory_order_relaxed);
        r4_h2p.ready.store(0, std::memory_order_relaxed);

        r1_p2h.head.store(0, std::memory_order_relaxed);
        r1_p2h.tail.store(0, std::memory_order_relaxed);
        r1_p2h.count.store(0, std::memory_order_relaxed);
        for (auto& b : r1_p2h.data) b.store(0, std::memory_order_relaxed);
        r2_p2h.value.store(0, std::memory_order_relaxed);
        r2_p2h.ready.store(0, std::memory_order_relaxed);
        r3_p2h.data[0].store(0, std::memory_order_relaxed);
        r3_p2h.data[1].store(0, std::memory_order_relaxed);
        r3_p2h.head.store(0, std::memory_order_relaxed);
        r3_p2h.tail.store(0, std::memory_order_relaxed);
        r3_p2h.count.store(0, std::memory_order_relaxed);
        r4_p2h.value.store(0, std::memory_order_relaxed);
        r4_p2h.ready.store(0, std::memory_order_relaxed);

        host_data_latch.store(0, std::memory_order_relaxed);
        parasite_data_latch.store(0, std::memory_order_relaxed);

        counters.r1_h2p_writes.store(0, std::memory_order_relaxed);
        counters.r1_h2p_reads.store(0, std::memory_order_relaxed);
        counters.r2_h2p_writes.store(0, std::memory_order_relaxed);
        counters.r2_h2p_reads.store(0, std::memory_order_relaxed);
        counters.r3_h2p_writes.store(0, std::memory_order_relaxed);
        counters.r3_h2p_reads.store(0, std::memory_order_relaxed);
        counters.r4_h2p_writes.store(0, std::memory_order_relaxed);
        counters.r4_h2p_reads.store(0, std::memory_order_relaxed);
        counters.r1_p2h_writes.store(0, std::memory_order_relaxed);
        counters.r1_p2h_reads.store(0, std::memory_order_relaxed);
        counters.r2_p2h_writes.store(0, std::memory_order_relaxed);
        counters.r2_p2h_reads.store(0, std::memory_order_relaxed);
        counters.r3_p2h_writes.store(0, std::memory_order_relaxed);
        counters.r3_p2h_reads.store(0, std::memory_order_relaxed);
        counters.r4_p2h_writes.store(0, std::memory_order_relaxed);
        counters.r4_p2h_reads.store(0, std::memory_order_relaxed);

        bus_stretch_cancel.store(false, std::memory_order_relaxed);
        debugger_stop_signal.store(false, std::memory_order_relaxed);
        io_pending_host.store(false, std::memory_order_relaxed);
        io_pending_parasite.store(false, std::memory_order_relaxed);
        host_command.store(0, std::memory_order_relaxed);
        parasite_ack.store(0, std::memory_order_relaxed);

        // Release fence so all relaxed stores are visible to other processes
        // that subsequently acquire-load any field.
        std::atomic_thread_fence(std::memory_order_release);
    }
};

// Verify that TubeShared has no implicit padding surprises.
// The exact size depends on alignment, but it must be trivially copyable
// for shared memory mapping.
static_assert(std::is_standard_layout_v<TubeShared>,
    "TubeShared must be standard layout for shared memory");

}  // namespace beebium
