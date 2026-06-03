// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "SerialDevice.hpp"
#include "../ClockTypes.hpp"
#include "../devices/Mc6850.hpp"

#include <cstdint>

namespace beebium {

// Serial ULA (SERPROC) emulation — the Ferranti serial processor at &FE10.
//
// On the BBC Micro the Serial ULA generates the transmit and receive bit
// clocks for the MC6850 ACIA, selects between the RS423 connector and the
// cassette interface, and drives the cassette motor relay. It is a write-only
// control latch:
//
//   bits 0-2: Transmit baud-rate select
//   bits 3-5: Receive baud-rate select
//   bit  6:   RS423 (1) / cassette (0) select  -> drives the ACIA /DCD input
//   bit  7:   Cassette motor relay
//
// In addition to decoding that latch, this class is the byte<->bit shifter
// between the ACIA's bit-level serial engine and the byte-oriented host
// transport (SerialDataSource / SerialDataSink):
//
//   Transmit (Beeb -> device): tick() pumps Mc6850::update_transmit() at the
//     transmit bit rate, assembles the data bits (LSB first) into a byte, and
//     delivers the byte to the sink on the stop bit.
//   Receive (device -> Beeb): tick() pulls a byte from the source and clocks it
//     into Mc6850::update_receive() one bit at a time (start, 8 data bits LSB
//     first, stop) at the receive bit rate. When idle it drives the receive
//     line high.
//
// Timing model: tick() is called once per 2MHz CPU cycle (see SerialSocket).
// The number of ticks per serial bit is CPU_HZ / baud, so e.g. 19200 baud is
// ~104 ticks/bit. This is a faithful bit-level model at an emulator-friendly
// rate; it is not cycle-accurate to the 16x ACIA sampling clock, but the BBC's
// software (and fujinet-nio) only care about correct byte framing at the
// selected rate.
class SerialUla {
public:
    // Control-register field masks.
    static constexpr uint8_t TX_BAUD_MASK   = 0x07;  // bits 0-2
    static constexpr uint8_t RX_BAUD_MASK   = 0x38;  // bits 3-5
    static constexpr uint8_t RX_BAUD_SHIFT  = 3;
    static constexpr uint8_t RS423_SELECT   = 0x40;  // bit 6
    static constexpr uint8_t MOTOR_ENABLE   = 0x80;  // bit 7

    // Baud rate indexed by the 3-bit select field (matches the BBC SERPROC
    // encoding used by OSBYTE 7/8 and b2's SERPROC_BAUD_RATES table).
    static constexpr unsigned BAUD_RATES[8] = {
        19200, 1200, 4800, 150, 9600, 300, 2400, 75,
    };

    explicit SerialUla(Mc6850& acia) : acia_(acia) {
        reset();
    }

    // Non-copyable (holds a reference and transport pointers).
    SerialUla(const SerialUla&) = delete;
    SerialUla& operator=(const SerialUla&) = delete;

    // Memory-mapped write (the latch ignores the address within its range).
    void write(uint16_t /*offset*/, uint8_t value) {
        control_ = value;
        tx_bit_period_ = bit_period(value & TX_BAUD_MASK);
        rx_bit_period_ = bit_period((value & RX_BAUD_MASK) >> RX_BAUD_SHIFT);

        // RS423 selected => carrier present (/DCD low). Cassette select (or
        // power-on default 0) holds /DCD high so the receiver stays idle.
        acia_.set_not_dcd((value & RS423_SELECT) == 0);
    }

    // The SERPROC has no read strobe; reads of &FE10 are open bus and are
    // handled by the owning socket. This method exists only so the latch can
    // satisfy a uniform device interface if needed.
    uint8_t read(uint16_t /*offset*/) const { return 0xFF; }

    // Advance the bit clocks by one 2MHz tick (called from SerialSocket).
    void tick() {
        // Transmit bit clock.
        if (++tx_timer_ >= tx_bit_period_) {
            tx_timer_ = 0;
            pump_transmit();
        }
        // Receive bit clock.
        if (++rx_timer_ >= rx_bit_period_) {
            rx_timer_ = 0;
            pump_receive();
        }
    }

    void reset() {
        control_ = 0;
        tx_bit_period_ = bit_period(0);
        rx_bit_period_ = bit_period(0);
        tx_timer_ = 0;
        rx_timer_ = 0;
        tx_byte_ = 0;
        tx_bit_index_ = 0;
        rx_state_ = RxByteState::Idle;
        rx_byte_ = 0;
        rx_bit_index_ = 0;
        rx_has_byte_ = false;
        // Match the power-on latch (cassette select => /DCD high).
        acia_.set_not_dcd(true);
    }

    // --- Transport wiring (non-owning; the SerialSocket owns the backend) ---
    void set_source(SerialDataSource* source) { source_ = source; }
    void set_sink(SerialDataSink* sink) {
        sink_ = sink;
        if (sink_ == nullptr) {
            // No device attached: clear to send (transmitted bytes are
            // discarded), so the guest's TX loop never stalls.
            acia_.set_not_cts(false);
        }
    }

    // --- Accessors for tests / debugging ---
    uint8_t control() const { return control_; }
    bool motor_on() const { return (control_ & MOTOR_ENABLE) != 0; }
    bool rs423_selected() const { return (control_ & RS423_SELECT) != 0; }
    unsigned tx_baud() const { return BAUD_RATES[control_ & TX_BAUD_MASK]; }
    unsigned rx_baud() const { return BAUD_RATES[(control_ & RX_BAUD_MASK) >> RX_BAUD_SHIFT]; }
    uint32_t tx_bit_period() const { return tx_bit_period_; }
    uint32_t rx_bit_period() const { return rx_bit_period_; }

private:
    // Bytes-to-bits receive framing state.
    enum class RxByteState : uint8_t { Idle, Start, Data, Stop };

    // Ticks (2MHz cycles) per serial bit at the given baud-select index.
    static uint32_t bit_period(uint8_t baud_index) {
        unsigned baud = BAUD_RATES[baud_index & 0x07];
        uint32_t period = static_cast<uint32_t>(timing::CPU_HZ / baud);
        return period < 1 ? 1 : period;
    }

    // One transmit bit time: clock the ACIA and assemble the outgoing byte.
    void pump_transmit() {
        // Drive /CTS from the device's flow-control state: when the device
        // cannot accept more, the ACIA holds TDRE low so the guest's transmit
        // loop busy-waits until the device drains. Refreshed each TX bit period,
        // so /CTS releases promptly once the device reports ready again.
        acia_.set_not_cts(sink_ != nullptr && !sink_->accepts_more());

        Mc6850::TransmitResult result = acia_.update_transmit();
        switch (result.type) {
            case Mc6850::BitType::Start:
                tx_byte_ = 0;
                tx_bit_index_ = 0;
                break;
            case Mc6850::BitType::Data:
                // LSB first.
                if (result.bit && tx_bit_index_ < 8) {
                    tx_byte_ |= static_cast<uint8_t>(1u << tx_bit_index_);
                }
                ++tx_bit_index_;
                break;
            case Mc6850::BitType::Stop:
                if (sink_) {
                    sink_->add_byte(tx_byte_);
                }
                break;
            default:
                break;
        }
    }

    // One receive bit time: shift the next received byte into the ACIA.
    void pump_receive() {
        uint8_t bit = 1;  // line idles high
        if (source_) {
            switch (rx_state_) {
                case RxByteState::Idle:
                    if (!rx_has_byte_ && source_->has_data()) {
                        rx_byte_ = source_->next_byte();
                        rx_has_byte_ = true;
                        rx_state_ = RxByteState::Start;
                    }
                    bit = 1;  // still idle this tick; start bit on the next
                    break;
                case RxByteState::Start:
                    bit = 0;  // start bit
                    rx_bit_index_ = 0;
                    rx_state_ = RxByteState::Data;
                    break;
                case RxByteState::Data:
                    bit = (rx_byte_ >> rx_bit_index_) & 1;
                    if (++rx_bit_index_ >= 8) {
                        rx_state_ = RxByteState::Stop;
                    }
                    break;
                case RxByteState::Stop:
                    bit = 1;  // stop bit
                    rx_state_ = RxByteState::Idle;
                    rx_has_byte_ = false;
                    break;
            }
        }
        acia_.update_receive(bit);
    }

    Mc6850& acia_;

    uint8_t control_ = 0;
    uint32_t tx_bit_period_ = 1;
    uint32_t rx_bit_period_ = 1;
    uint32_t tx_timer_ = 0;
    uint32_t rx_timer_ = 0;

    // Transmit byte assembly
    uint8_t tx_byte_ = 0;
    uint8_t tx_bit_index_ = 0;

    // Receive byte framing
    RxByteState rx_state_ = RxByteState::Idle;
    uint8_t rx_byte_ = 0;
    uint8_t rx_bit_index_ = 0;
    bool rx_has_byte_ = false;

    SerialDataSource* source_ = nullptr;
    SerialDataSink* sink_ = nullptr;
};

}  // namespace beebium
