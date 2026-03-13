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

// Host-side adapter for the Tube ULA over shared memory.
//
// Satisfies TubeHostInterface by operating on a TubeShared* pointer.
// This is the cross-process counterpart of TubeUla: TubeUla models
// both sides in a single process, while TubeHostPort models only the
// host's view, communicating with a parasite process through atomics.
//
// Ownership: TubeHostPort does NOT own the TubeShared memory. The caller
// (TubeSocket or shared memory manager) is responsible for its lifetime.
//
// Memory ordering: stores use release, loads use acquire. This ensures
// that data written by one side is visible when the other side reads
// the corresponding flag/count.

class TubeHostPort {
public:
    explicit TubeHostPort(TubeShared* shared)
        : shared_(shared)
    {
        assert(shared_ != nullptr);
    }

    // --- TubeHostInterface ---

    uint8_t host_read(uint8_t offset);
    void host_write(uint8_t offset, uint8_t value);
    bool hirq() const;
    void reset();

    // --- Accessors for testing ---

    TubeShared* shared() { return shared_; }
    const TubeShared* shared() const { return shared_; }

    uint8_t control_flags() const {
        return shared_->control_flags.load(std::memory_order_acquire);
    }

private:
    // Read a byte from the R1 P-to-H FIFO (24-byte ring buffer).
    // Returns 0 if the FIFO is empty.
    uint8_t dequeue_r1_p2h();

    // Read a byte from the R3 P-to-H register (shift down).
    // Returns 0 if empty.
    uint8_t dequeue_r3_p2h();

    // Clear all register data (soft reset). Preserves header and control flags.
    void soft_reset();

    TubeShared* shared_;
};

}  // namespace beebium
