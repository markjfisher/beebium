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

#ifndef BEEBIUM_USER_PORT_DEVICE_HPP
#define BEEBIUM_USER_PORT_DEVICE_HPP

#include <cstdint>

namespace beebium {

// Interface for devices attached to the BBC Micro's User Port.
//
// The User Port exposes VIA Port B data lines (PB0-PB7) and control
// lines CB1 and CB2. This interface deliberately does NOT extend
// ViaPeripheral -- it exposes only User Port signals and has no
// access to Port A or CA1/CA2.
//
// Only one UserPortDevice can be attached at a time (the User Port
// has a single connector, unlike the 1 MHz bus which supports
// daisy-chaining).
struct UserPortDevice {
    virtual ~UserPortDevice() = default;

    // Called when Port B output or DDR changes, and periodically on
    // VIA clock ticks. The device returns the state of input pins
    // (bits where DDR=0 are read by the VIA).
    //
    // output: the VIA's Port B output register value
    // ddr:    the VIA's Port B data direction register (1=output, 0=input)
    virtual uint8_t update_port_b(uint8_t output, uint8_t ddr) = 0;

    // Called to update CB1/CB2 control line states.
    //
    // CB1 is always an input to the VIA. The VIA resets CB1 to 1 after
    // each tick; the device can pull it low to signal an event (edge
    // detection triggers CB1 interrupt per PCR configuration).
    //
    // CB2 mode depends on VIA PCR bits 5-7:
    //   Input modes:  device drives CB2, VIA detects edges
    //   Output modes: VIA drives CB2 (handshake, pulse, manual)
    //                 device can read CB2 to see VIA-driven state
    virtual void update_control_lines(uint8_t& cb1, uint8_t& cb2) {
        (void)cb1; (void)cb2;
    }
};

}  // namespace beebium

#endif  // BEEBIUM_USER_PORT_DEVICE_HPP
