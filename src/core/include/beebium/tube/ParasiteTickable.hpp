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

#include <cstdint>

namespace beebium {

// Minimal interface for a parasite processor that can be ticked from
// Machine::step(). Implemented by ParasiteRunner; used by TubeSocket
// to drive the parasite in the single-threaded interleaved model.
class ParasiteTickable {
public:
    virtual ~ParasiteTickable() = default;

    // Execute one parasite CPU cycle.
    virtual void tick() = 0;

    // Returns true if the debugger has paused this processor.
    // TubeSocket skips ticking when paused.
    virtual bool is_paused() const = 0;

    // Reset the parasite processor.
    //
    // On real hardware, the BBC's reset line propagates through the Tube
    // cable and resets the parasite CPU together with the host. The host's
    // TubeSocket::reset() invokes this so the parasite restarts at the
    // reset vector instead of resuming whatever it was doing before Break.
    // Default is a no-op so tests with simple stub tickables don't have to
    // implement it.
    virtual void reset() {}

    // Diagnostic: current parasite PC for stretch deadlock investigation.
    // Default returns 0xFFFF (sentinel) for implementations that don't override.
    virtual uint16_t diag_pc() const { return 0xFFFF; }
};

}  // namespace beebium
