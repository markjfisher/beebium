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

#ifndef BEEBIUM_EXTENSION_ONE_MHZ_BUS_DEVICE_HPP
#define BEEBIUM_EXTENSION_ONE_MHZ_BUS_DEVICE_HPP

#include <cstdint>

namespace beebium {

// Callback interface for devices attached to the 1 MHz bus (FRED/JIM region).
//
// The offset parameter is relative to 0xFC00 (the start of the FRED page),
// matching the offset that MemoryMap's Region binding computes (addr - Base).
// FRED occupies offsets 0x0000-0x00FF, JIM occupies 0x0100-0x01FF.
struct OneMHzBusDevice {
    virtual ~OneMHzBusDevice() = default;

    virtual uint8_t read(uint16_t offset) = 0;
    virtual void write(uint16_t offset, uint8_t value) = 0;
    virtual void tick() {}

    // IRQ support. Devices that generate interrupts override this to
    // return true when their IRQ line is asserted. The OneMHzBusPort
    // aggregates IRQ status from all attached devices.
    virtual bool irq_pending() const { return false; }
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ONE_MHZ_BUS_DEVICE_HPP
