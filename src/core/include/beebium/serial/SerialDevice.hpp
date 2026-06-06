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

// Byte-oriented serial transport seam.
//
// The SerialUla serialises ACIA transmit bits into whole bytes and pushes them
// to a SerialDataSink (Beeb -> device), and pulls whole bytes from a
// SerialDataSource (device -> Beeb) and shifts them back into the ACIA as bits.
//
// These interfaces decouple the bit-level serial engine in the emulator core
// from the concrete device, which lives in a PeripheralExtension (the
// host-serial bridge, the loopback plug, the rpc-serial peer, a future
// FujiNet, ...). Implementations that bridge to a background I/O thread must be
// internally thread-safe; the methods here are called from the emulation thread.

// Device -> Beeb (the ULA reads received bytes from here).
class SerialDataSource {
public:
    virtual ~SerialDataSource() = default;

    // True if a byte is available to receive.
    virtual bool has_data() = 0;

    // Remove and return the next received byte. Only call when has_data().
    virtual uint8_t next_byte() = 0;

    // Optional: the device signals that a BREAK condition should be delivered to
    // the BBC's receiver. A BREAK is a sub-byte line condition (the line held in
    // the space state past a stop bit), not a data byte, so it is carried here
    // out of band rather than through next_byte(). The Serial ULA polls this
    // before has_data(); when it returns true (consuming the request) the ULA
    // clocks a framing-error frame into the ACIA (data 0x00 + Framing Error),
    // which is exactly how the MC6850 surfaces a received break. Default: never.
    virtual bool take_break() { return false; }
};

// Beeb -> device (the ULA writes transmitted bytes here).
class SerialDataSink {
public:
    virtual ~SerialDataSink() = default;

    // Accept one transmitted byte.
    virtual void add_byte(uint8_t value) = 0;

    // Flow control, modelling the /CTS line. Returns false when the device
    // cannot accept more transmitted bytes right now; the Serial ULA then
    // asserts the ACIA's /CTS input, which holds TDRE = 0 so the BBC's transmit
    // loop busy-waits (it stalls the emulated guest, never the emulator host).
    // Default: always clear to send.
    virtual bool accepts_more() { return true; }

    // The BBC's outbound RTS line, the only other control signal on the RS423
    // connector besides CTS (there is no DCD/DTR/DSR pin). The MOS raises RTS to
    // halt incoming data when its RS423 input buffer is nearly full; a network
    // device may also use it as a connection cue. Driven by the MC6850
    // Transmitter Control field (control register bits 5-6).
    //
    // rts_asserted == true  => /RTS active (pin low)  => BBC requesting the link.
    // rts_asserted == false => /RTS deasserted (pin high).
    //
    // The Serial ULA delivers the current level once when the device is attached
    // and thereafter on every transition. Default: ignore (most devices do not
    // care about RTS). Called on the emulation thread.
    virtual void set_rts(bool /*rts_asserted*/) {}

    // The BBC's outbound BREAK state, driven by the MC6850 Transmitter Control
    // field (the guest sets bits 5-6 = 11 to hold the TX line in the space state
    // -- a break -- and clears them to end it). Like set_rts this is a line
    // condition rather than data, delivered as a baseline at attach and on every
    // transition. break_asserted == true while the guest is transmitting a break.
    // Default: ignore. Called on the emulation thread.
    virtual void set_break(bool /*break_asserted*/) {}
};

// The seam a serial device attaches to: a single, bidirectional endpoint that
// the Serial ULA both receives from (device -> Beeb) and transmits to
// (Beeb -> device). This is the serial analogue of UserPortDevice /
// OneMHzBusDevice -- the one interface an attached device implements, whether a
// host-serial bridge or an emulated device (e.g. a FujiNet). It combines the two
// half-seams the bit engine uses internally; SerialSocket accepts a
// SerialPortDevice and wires it to the ULA as both source and sink.
class SerialPortDevice : public SerialDataSource, public SerialDataSink {
};

}  // namespace beebium
