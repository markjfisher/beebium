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

#include "NetworkBackend.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace beebium {

// Motorola MC6854 Advanced Data Link Controller (ADLC) emulation.
//
// The MC6854 is the core networking chip in the BBC Micro's Econet hardware.
// It implements HDLC-like framing with a 3-byte FIFO for both transmit and receive.
//
// Register addressing:
//   Offset 0 read:  Status Register 1 (SR1)
//   Offset 0 write: Control Register 1 (CR1)
//   Offset 1 read:  Status Register 2 (SR2)
//   Offset 1 write: CR2 (when CR1 bit 0 = 0) or CR3 (when CR1 bit 0 = 1)
//   Offset 2 read:  Receive FIFO
//   Offset 2 write: Transmit FIFO (frame continue)
//   Offset 3 read:  Receive FIFO
//   Offset 3 write: TX FIFO frame terminate (when CR1 bit 0 = 0) or CR4 (when CR1 bit 0 = 1)
//
// FIFO entries are uint16_t with metadata in upper bits:
//   Bits 0-7:  Data byte
//   Bit 8:     Valid entry (0x0100)
//   Bit 9:     Last byte of frame (0x0200)
//   Bit 10:    Address Present — set on address field bytes (0x0400)
//
// Clocked at 2MHz (E clock), with both rising and falling edges used.
class Mc6854 {
public:
    // FIFO entry metadata flags
    static constexpr uint16_t FIFO_VALID  = 0x0100;
    static constexpr uint16_t FIFO_LAST   = 0x0200;
    static constexpr uint16_t FIFO_AP     = 0x0400;

    // FIFO capacity
    static constexpr int FIFO_SIZE = 3;

    // Control Register 1 bits
    static constexpr uint8_t CR1_AC           = 0x01;  // Address Control
    static constexpr uint8_t CR1_RIE          = 0x02;  // Receiver Interrupt Enable
    static constexpr uint8_t CR1_TIE          = 0x04;  // Transmitter Interrupt Enable
    static constexpr uint8_t CR1_RDSR         = 0x08;  // RDSR Mode (DMA)
    static constexpr uint8_t CR1_TDSR         = 0x10;  // TDSR Mode (DMA)
    static constexpr uint8_t CR1_DISCONTINUE  = 0x20;  // Rx Frame Discontinue (auto-clear)
    static constexpr uint8_t CR1_RX_RESET     = 0x40;  // Receiver Reset
    static constexpr uint8_t CR1_TX_RESET     = 0x80;  // Transmitter Reset

    // Control Register 2 bits
    static constexpr uint8_t CR2_PSE          = 0x01;  // Prioritised Status Enable
    static constexpr uint8_t CR2_2_1_BYTE     = 0x02;  // 2-byte/1-byte transfer
    static constexpr uint8_t CR2_FLAG_IDLE    = 0x04;  // Flag/Mark Idle mode
    static constexpr uint8_t CR2_FC_TDRA      = 0x08;  // Frame Complete / TDRA select
    static constexpr uint8_t CR2_CLR_RX_ST    = 0x10;  // Clear Rx Status (auto-clear)
    static constexpr uint8_t CR2_CLR_TX_ST    = 0x20;  // Clear Tx Status (auto-clear)
    static constexpr uint8_t CR2_RTS          = 0x40;  // Request To Send control
    static constexpr uint8_t CR2_RTS_CONTROL  = 0x80;  // RTS control mode

    // Control Register 3 bits
    static constexpr uint8_t CR3_LCF          = 0x01;  // Logical Control Field select
    static constexpr uint8_t CR3_AEX          = 0x02;  // Address Extend mode
    static constexpr uint8_t CR3_IDLE_8_1     = 0x04;  // Idle condition (8 ones / 1 flag)
    static constexpr uint8_t CR3_FD_ENABLE    = 0x08;  // Flag Detected enable
    static constexpr uint8_t CR3_LOOP         = 0x10;  // Loop mode
    static constexpr uint8_t CR3_GO_ACTIVE    = 0x20;  // Go Active in loop
    static constexpr uint8_t CR3_LOOP_EXT     = 0x40;  // Loop/On-loop Extended
    static constexpr uint8_t CR3_ADDR_MASK    = 0x80;  // Address mask control

    // Control Register 4 bits
    static constexpr uint8_t CR4_DBL_FLAG     = 0x01;  // Double Flag interframe
    static constexpr uint8_t CR4_WORD_8_6     = 0x02;  // Word length: 8 or 6 bits
    static constexpr uint8_t CR4_RX_WORD_8_6  = 0x04;  // Rx Word length
    static constexpr uint8_t CR4_TX_ABT       = 0x08;  // Transmit Abort
    static constexpr uint8_t CR4_ABT_EXT      = 0x10;  // Abort Extend (auto-clear)
    static constexpr uint8_t CR4_NRZ_NRZI     = 0x20;  // NRZ/NRZI encoding
    static constexpr uint8_t CR4_TX_CRC_INH   = 0x40;  // Transmit CRC Inhibit
    static constexpr uint8_t CR4_RX_CRC_INH   = 0x80;  // Receive CRC Inhibit

    // Status Register 1 bits
    static constexpr uint8_t SR1_RDA          = 0x01;  // Receiver Data Available
    static constexpr uint8_t SR1_S2RQ         = 0x02;  // Status #2 Read Request
    static constexpr uint8_t SR1_LOOP         = 0x04;  // Loop Status
    static constexpr uint8_t SR1_FD           = 0x08;  // Flag Detected
    static constexpr uint8_t SR1_CTS          = 0x10;  // Clear To Send (active low)
    static constexpr uint8_t SR1_TXU          = 0x20;  // Transmitter Underrun
    static constexpr uint8_t SR1_TDRA         = 0x40;  // TX Data Register Available
    static constexpr uint8_t SR1_IRQ          = 0x80;  // Interrupt Request

    // Status Register 2 bits
    static constexpr uint8_t SR2_AP           = 0x01;  // Address Present
    static constexpr uint8_t SR2_FV           = 0x02;  // Frame Valid
    static constexpr uint8_t SR2_INACTIVE     = 0x04;  // Rx Idle (Inactive)
    static constexpr uint8_t SR2_ABT          = 0x08;  // Rx Abort
    static constexpr uint8_t SR2_ERR          = 0x10;  // FCS Error (CRC)
    static constexpr uint8_t SR2_DCD          = 0x20;  // Data Carrier Detect
    static constexpr uint8_t SR2_OVRN         = 0x40;  // Rx Overrun
    static constexpr uint8_t SR2_RDA          = 0x80;  // Receiver Data Available

    // Frame field states — tracks which part of a frame we're processing
    enum class FrameField : uint8_t {
        Idle     = 0,  // Between frames
        Flag     = 1,  // Flag sequence
        Address  = 2,  // Address field (first byte, or extended)
        Control  = 3,  // Control byte
        ExtCtrl  = 4,  // Extended control byte (if LCF set)
        Lcf      = 5,  // Logical Control Field
        Data     = 6,  // Data portion of frame
    };

    explicit Mc6854(NetworkBackend& backend)
        : backend_(backend)
    {
        hard_reset();
    }

    ~Mc6854() = default;

    // Non-copyable
    Mc6854(const Mc6854&) = delete;
    Mc6854& operator=(const Mc6854&) = delete;

    // Register read (offset masked to 2 bits: 0-3)
    uint8_t read(uint16_t offset) {
        switch (offset & 0x03) {
            case 0: return read_sr1();
            case 1: return read_sr2();
            case 2: return read_rx_fifo();
            case 3: return read_rx_fifo();
            default: return 0x00;
        }
    }

    // Register write (offset masked to 2 bits: 0-3)
    void write(uint16_t offset, uint8_t value) {
        switch (offset & 0x03) {
            case 0: write_cr1(value); break;
            case 1: write_cr2_or_cr3(value); break;
            case 2: write_tx_fifo(value, false); break;  // Frame continue
            case 3: write_cr4_or_tx_last(value); break;
            default: break;
        }
    }

    // 2MHz clock edges — called on every E-clock transition
    void tick_rising() {
        advance_byte_timer();
        update_status();
    }

    void tick_falling() {
        advance_byte_timer();
        update_status();
    }

    // Set byte trickle period (in 2MHz half-cycles). Default 128 (~32us per byte).
    void set_byte_period(int period) { byte_period_ = period; }
    int byte_period() const { return byte_period_; }

    // External CTS input (active low: false = CTS asserted = clear to send)
    void set_cts_input(bool cts_high) {
        cts_input_ = cts_high;
    }

    bool cts_input() const { return cts_input_; }

    // Hard reset — returns all state to power-on defaults
    void hard_reset() {
        cr1_ = CR1_RX_RESET | CR1_TX_RESET;  // Both sections held in reset
        cr2_ = 0;
        cr3_ = 0;
        cr4_ = 0;
        sr1_ = 0;
        sr2_ = 0;
        clear_tx_fifo();
        clear_rx_fifo();
        tx_frame_field_ = FrameField::Idle;
        rx_frame_field_ = FrameField::Idle;
        tx_frame_buffer_.clear();
        rx_frame_buffer_.clear();
        rx_buffer_index_ = 0;
        irq_output_ = false;

        // Clear all stored status latches
        fd_stored_ = false;
        cts_stored_ = false;
        txu_stored_ = false;

        fv_stored_ = false;
        err_stored_ = false;
        abt_stored_ = false;
        ovrn_stored_ = false;
        idle_stored_ = false;
        dcd_stored_ = false;

        // Initialize DCD edge detection to match the current backend state.
        // This prevents a spurious edge on the first tick: if the backend is
        // already disconnected (no carrier), we don't want the edge-triggered
        // latch to fire, since the level-sensitive SR2 DCD path already handles
        // the "carrier continuously absent" case for NFS "No Clock" detection.
        prev_dcd_input_ = !backend_.is_connected();
        prev_cts_input_ = false;
        cts_input_ = false;

        // Reset PSE level
        pse_level_ = 0;

        // Reset byte timer
        byte_timer_ = 0;

    }

    // IRQ output pin (directly drives NMI via BBC glue logic)
    bool irq_output() const { return irq_output_; }

    // Access control registers (for test inspection)
    uint8_t cr1() const { return cr1_; }
    uint8_t cr2() const { return cr2_; }
    uint8_t cr3() const { return cr3_; }
    uint8_t cr4() const { return cr4_; }
    uint8_t sr1() const { return sr1_; }
    uint8_t sr2() const { return sr2_; }

    // Access frame field state (for test inspection)
    FrameField tx_frame_field() const { return tx_frame_field_; }
    FrameField rx_frame_field() const { return rx_frame_field_; }

    // PSE priority level (for test inspection): 0=inactive, 1-4=priority tiers
    int pse_level() const { return pse_level_; }

    // TX FIFO state inspection
    bool tx_fifo_empty() const {
        for (int i = 0; i < FIFO_SIZE; ++i) {
            if (tx_fifo_[i] & FIFO_VALID) return false;
        }
        return true;
    }

    bool tx_fifo_full() const {
        return (tx_fifo_[FIFO_SIZE - 1] & FIFO_VALID) != 0;
    }

    // RX FIFO state inspection
    bool rx_fifo_empty() const {
        return (rx_fifo_[0] & FIFO_VALID) == 0;
    }

    bool rx_fifo_full() const {
        return (rx_fifo_[FIFO_SIZE - 1] & FIFO_VALID) != 0;
    }

private:
    // --- Register read/write ---

    uint8_t read_sr1() {
        return sr1_;
    }

    uint8_t read_sr2() {
        return sr2_;
    }

    uint8_t read_rx_fifo() {
        if (rx_fifo_[0] & FIFO_VALID) {
            uint8_t data = rx_fifo_[0] & 0xFF;
            shift_rx_fifo();
            update_status();
            return data;
        }
        return 0x00;
    }

    void write_cr1(uint8_t value) {
        // TX Reset
        if (value & CR1_TX_RESET) {
            clear_tx_fifo();
            tx_frame_field_ = FrameField::Idle;
            tx_frame_buffer_.clear();
            txu_stored_ = false;
            cts_stored_ = false;
        }

        // RX Reset
        if (value & CR1_RX_RESET) {
            clear_rx_fifo();
            rx_frame_field_ = FrameField::Idle;
            rx_frame_buffer_.clear();
            rx_buffer_index_ = 0;
            fd_stored_ = false;

            fv_stored_ = false;
            err_stored_ = false;
            abt_stored_ = false;
            ovrn_stored_ = false;
            idle_stored_ = false;
            dcd_stored_ = false;
            pse_level_ = 0;
        }

        // Rx Frame Discontinue (CR1b5) — auto-clearing
        // When set, discards current RX frame and resets to idle
        if (value & CR1_DISCONTINUE) {
            clear_rx_fifo();
            rx_frame_field_ = FrameField::Idle;
            rx_frame_buffer_.clear();
            rx_buffer_index_ = 0;
            fv_stored_ = false;

            // Bit auto-clears — don't store it
            value &= ~CR1_DISCONTINUE;
        }

        cr1_ = value;
        update_status();
    }

    void write_cr2_or_cr3(uint8_t value) {
        if (cr1_ & CR1_AC) {
            // AC=1: write to CR3
            cr3_ = value;
        } else {
            // AC=0: write to CR2
            // Handle auto-clearing bits before storing
            uint8_t auto_clear_mask = CR2_CLR_RX_ST | CR2_CLR_TX_ST;

            if (value & CR2_CLR_RX_ST) {
                // Clear stored RX status conditions
                fv_stored_ = false;
                err_stored_ = false;
                abt_stored_ = false;
                ovrn_stored_ = false;
                idle_stored_ = false;
                dcd_stored_ = false;
                fd_stored_ = false;
                // Advance PSE priority level
                if ((cr2_ & CR2_PSE) && pse_level_ < 4) {
                    ++pse_level_;
                }
            }

            if (value & CR2_CLR_TX_ST) {
                // Clear stored TX status conditions
                cts_stored_ = false;
                txu_stored_ = false;
            }

            // Store CR2 without auto-clearing bits
            cr2_ = value & ~auto_clear_mask;
        }
        update_status();
    }

    void write_cr4_or_tx_last(uint8_t value) {
        if (cr1_ & CR1_AC) {
            // AC=1: write to CR4
            // Abort Extend (CR4b4) is auto-clearing
            cr4_ = value & ~CR4_ABT_EXT;
        } else {
            // AC=0: write TX data with last-byte flag
            write_tx_fifo(value, true);
        }
    }

    void write_tx_fifo(uint8_t value, bool is_last) {
        // TX must not be in reset
        if (cr1_ & CR1_TX_RESET) return;

        // Push into FIFO — find first empty slot
        uint16_t entry = FIFO_VALID | value;
        if (is_last) entry |= FIFO_LAST;

        // Track frame field state for AP marking
        if (tx_frame_field_ == FrameField::Idle || tx_frame_field_ == FrameField::Flag) {
            // First byte of a new frame is an address byte
            tx_frame_field_ = FrameField::Address;
            entry |= FIFO_AP;
        } else if (tx_frame_field_ == FrameField::Address) {
            // Extended addressing: if AEX set and bit 0 of previous address byte was 0,
            // this is still an address byte. For simplicity in Phase 1, advance after
            // first address byte unless AEX is set.
            if (cr3_ & CR3_AEX) {
                // In AEX mode, address continues while bit 0 of previous byte was 0.
                // We track this via the last written byte — check if we should stay in address.
                // For now: mark as address, advance when bit 0 is set.
                if (value & 0x01) {
                    // Bit 0 set: last address byte
                    tx_frame_field_ = FrameField::Control;
                }
                entry |= FIFO_AP;
            } else {
                // Standard 2-byte addressing: second byte is also address
                tx_frame_field_ = FrameField::Control;
                entry |= FIFO_AP;
            }
        } else if (tx_frame_field_ == FrameField::Control) {
            if ((cr3_ & CR3_LCF) && !(cr3_ & CR3_AEX)) {
                tx_frame_field_ = FrameField::Lcf;
            } else {
                tx_frame_field_ = FrameField::Data;
            }
        } else if (tx_frame_field_ == FrameField::Lcf) {
            tx_frame_field_ = FrameField::Data;
        }
        // Data field: stays in Data

        // Push to FIFO
        push_tx_fifo(entry);

        // If this was the last byte, reset frame field for next frame
        if (is_last) {
            tx_frame_field_ = FrameField::Idle;
        }

        update_status();
    }

    // --- Byte trickle timer ---

    void advance_byte_timer() {
        if (++byte_timer_ >= byte_period_) {
            byte_timer_ = 0;
            tx_process_byte();
            rx_process_byte();
        }
    }

    // TX: Pull one byte from TX FIFO into frame buffer. When last-byte flag seen, send.
    void tx_process_byte() {
        if (cr1_ & CR1_TX_RESET) return;

        if (tx_fifo_[0] & FIFO_VALID) {
            uint16_t entry = pop_tx_fifo();
            uint8_t data = entry & 0xFF;
            tx_frame_buffer_.push_back(data);

            if (entry & FIFO_LAST) {
                // Frame complete — send to backend
                on_tx_frame_complete();
            }
        } else if (!tx_frame_buffer_.empty()) {
            // FIFO empty mid-frame → TX underrun
            txu_stored_ = true;
            tx_frame_buffer_.clear();
        }
    }

    // Called when a complete frame has been assembled in tx_frame_buffer_.
    void on_tx_frame_complete() {
        NetworkFrame nf;
        nf.type = FrameType::RawFrame;
        nf.data = tx_frame_buffer_;
        backend_.send_frame(nf);
        tx_frame_buffer_.clear();
    }

    // RX: If FIFO has space and no FV blocking, push from rx_frame_buffer_.
    void rx_process_byte() {
        if (cr1_ & CR1_RX_RESET) return;

        // If FV is set, don't push more data until it's cleared
        if (fv_stored_) return;

        // Try to fetch a new frame if we have no buffered frame
        if (rx_buffer_index_ >= rx_frame_buffer_.size()) {
            auto frame = backend_.receive_frame();
            if (frame) {
                rx_frame_buffer_ = std::move(frame->data);
                rx_buffer_index_ = 0;
                rx_frame_field_ = FrameField::Idle;
            } else {
                return;  // Nothing to receive
            }
        }

        // Don't push if FIFO is full
        if (rx_fifo_full()) {
            ovrn_stored_ = true;
            return;
        }

        // Push one byte from frame buffer into FIFO
        uint8_t data = rx_frame_buffer_[rx_buffer_index_];
        bool is_last = (rx_buffer_index_ == rx_frame_buffer_.size() - 1);

        uint16_t entry = FIFO_VALID | data;

        // Track frame field for AP marking
        if (rx_frame_field_ == FrameField::Idle) {
            rx_frame_field_ = FrameField::Address;
            entry |= FIFO_AP;
        } else if (rx_frame_field_ == FrameField::Address) {
            if (cr3_ & CR3_AEX) {
                if (data & 0x01) {
                    rx_frame_field_ = FrameField::Control;
                }
                entry |= FIFO_AP;
            } else {
                rx_frame_field_ = FrameField::Control;
                entry |= FIFO_AP;
            }
        } else if (rx_frame_field_ == FrameField::Control) {
            rx_frame_field_ = FrameField::Data;
        }

        if (is_last) {
            entry |= FIFO_LAST;
            rx_frame_field_ = FrameField::Idle;
        }

        push_rx_fifo(entry);
        ++rx_buffer_index_;

        // If we just pushed the last byte, mark FV
        if (is_last) {
            fv_stored_ = true;
        }
    }

    // --- FIFO operations ---

    void push_tx_fifo(uint16_t entry) {
        // Shift-register style: find first empty slot
        for (int i = 0; i < FIFO_SIZE; ++i) {
            if (!(tx_fifo_[i] & FIFO_VALID)) {
                tx_fifo_[i] = entry;
                return;
            }
        }
        // FIFO full — entry is lost (TX overrun condition)
    }

    uint16_t pop_tx_fifo() {
        uint16_t entry = tx_fifo_[0];
        shift_tx_fifo();
        return entry;
    }

    void shift_tx_fifo() {
        for (int i = 0; i < FIFO_SIZE - 1; ++i) {
            tx_fifo_[i] = tx_fifo_[i + 1];
        }
        tx_fifo_[FIFO_SIZE - 1] = 0;  // Clear last slot
    }

    void push_rx_fifo(uint16_t entry) {
        for (int i = 0; i < FIFO_SIZE; ++i) {
            if (!(rx_fifo_[i] & FIFO_VALID)) {
                rx_fifo_[i] = entry;
                return;
            }
        }
        // FIFO full — overrun
        ovrn_stored_ = true;
    }

    void shift_rx_fifo() {
        for (int i = 0; i < FIFO_SIZE - 1; ++i) {
            rx_fifo_[i] = rx_fifo_[i + 1];
        }
        rx_fifo_[FIFO_SIZE - 1] = 0;
    }

    void clear_tx_fifo() {
        for (int i = 0; i < FIFO_SIZE; ++i) tx_fifo_[i] = 0;
    }

    void clear_rx_fifo() {
        for (int i = 0; i < FIFO_SIZE; ++i) rx_fifo_[i] = 0;
    }

    // --- Status update ---
    //
    // Status bits are categorised as:
    //   Present:    Continuously derived from hardware state (RDA, TDRA, Loop)
    //   Stored:     Latched on transitions, CPU-cleared (FD, CTS, TxU, AP, FV, ERR, OVRN)
    //   Dual-nature: OR of stored latch + present input (DCD, RxABT, Rx Idle)

    void update_status() {
        // --- Present conditions (derived from current state) ---

        // RDA: data available in RX FIFO
        bool rda = (rx_fifo_[0] & FIFO_VALID) != 0;

        // AP: address present on current RX byte — purely derived from FIFO head
        // (not an independent stored latch; reflects whether the byte at the FIFO
        // output has the AP metadata flag set)
        bool ap_present = rda && (rx_fifo_[0] & FIFO_AP) != 0;

        // FV: frame valid — last byte with good CRC in FIFO (stored until cleared)
        bool fv_present = false;
        for (int i = 0; i < FIFO_SIZE; ++i) {
            if ((rx_fifo_[i] & FIFO_VALID) && (rx_fifo_[i] & FIFO_LAST)) {
                fv_present = true;
                break;
            }
        }

        // TDRA: TX data register available
        // True when TX FIFO has space, TX not in reset, AND CTS is not inhibiting
        bool tdra = !tx_fifo_full() && !(cr1_ & CR1_TX_RESET) && !cts_input_;

        // DCD present: reflects backend connection (DCD=1 when disconnected)
        bool dcd_present = !backend_.is_connected();

        // Rx Idle present: line is idle when RX not in reset, FIFO empty, no FV,
        // and no pending data in the frame buffer
        bool idle_present = !(cr1_ & CR1_RX_RESET)
            && rx_fifo_empty()
            && !fv_stored_
            && (rx_buffer_index_ >= rx_frame_buffer_.size());

        // Flag fill from the backend suppresses idle (line appears busy)
        bool idle_condition = idle_present && !backend_.is_receiving_flags();

        // Rx Abort present: (placeholder — not simulated over AUN/UDP)
        bool abt_present = false;

        // CTS present: reflects external CTS input (active low: high = not clear to send)
        bool cts_present = cts_input_;

        // Flag Detected: driven by backend flag fill, or by stored latch
        bool fd_condition = fd_stored_ || backend_.is_receiving_flags();

        // --- Stored condition edge detection ---

        // DCD stored: latch on positive edge (0→1 transition of DCD input)
        if (dcd_present && !prev_dcd_input_) {
            dcd_stored_ = true;
        }
        prev_dcd_input_ = dcd_present;

        // CTS stored: latch on positive edge
        if (cts_present && !prev_cts_input_) {
            cts_stored_ = true;
        }
        prev_cts_input_ = cts_present;

        // FV stored: latch when frame valid condition appears
        if (fv_present) {
            fv_stored_ = true;
        }

        // --- Build SR2 ---

        // DCD in SR2 has two components:
        //  1. Edge-triggered latch: set on carrier-loss transition, cleared by
        //     CLR_RX_ST or RX_RESET. Drives S2RQ and thus IRQ generation.
        //  2. Current pin level: reflects continuous carrier state for polling.
        //     When the receiver is active (!RX_RESET) and no carrier is present,
        //     the DCD pin is HIGH, and SR2 DCD reflects this directly.
        //
        // The NFS ROM polls SR2 DCD during boot to detect "No Clock" — it needs
        // to see DCD=1 whenever carrier is absent, not just on transitions.
        // S2RQ uses only the edge-triggered latch to prevent NMI storms from
        // continuously absent carrier (as would happen with --aun-port none).
        bool dcd_for_sr2 = dcd_stored_ || (dcd_present && !(cr1_ & CR1_RX_RESET));
        bool dcd_for_s2rq = dcd_stored_;  // Edge-triggered only for IRQ path
        bool abt_bit = abt_stored_ || abt_present;
        bool idle_bit = (idle_stored_ || idle_condition);

        uint8_t sr2_raw = 0;
        if (ap_present)                sr2_raw |= SR2_AP;
        if (fv_stored_ || fv_present)  sr2_raw |= SR2_FV;
        if (idle_bit)                  sr2_raw |= SR2_INACTIVE;
        if (abt_bit)                   sr2_raw |= SR2_ABT;
        if (err_stored_)               sr2_raw |= SR2_ERR;
        if (dcd_for_sr2)               sr2_raw |= SR2_DCD;
        if (ovrn_stored_)              sr2_raw |= SR2_OVRN;
        if (rda)                       sr2_raw |= SR2_RDA;

        // PSE filtering: when CR2b0 set, suppress lower-priority bits
        if (cr2_ & CR2_PSE) {
            sr2_ = apply_pse_filter(sr2_raw);
        } else {
            sr2_ = sr2_raw;
        }

        // --- Build SR1 ---

        // S2RQ: OR of DCD, OVRN, ABT, FV, AP, ERR (per MC6854 datasheet).
        // INACTIVE and RDA do NOT participate in S2RQ.
        // Uses the edge-triggered DCD latch (not the level-sensitive SR2 DCD)
        // to prevent NMI storms when carrier is continuously absent.
        bool s2rq_bits = (sr2_ & (SR2_OVRN | SR2_ABT | SR2_FV | SR2_AP | SR2_ERR)) != 0;
        bool s2rq = s2rq_bits || dcd_for_s2rq;

        sr1_ = (rda ? SR1_RDA : 0)
             | (s2rq ? SR1_S2RQ : 0)
             | (fd_condition ? SR1_FD : 0)
             | ((cts_stored_ || cts_present) ? SR1_CTS : 0)
             | (txu_stored_ ? SR1_TXU : 0)
             | (tdra ? SR1_TDRA : 0);

        // --- IRQ output (level-sensitive) ---
        //
        // The ADLC IRQ pin is a level output: asserted when any enabled cause is active.
        // Edge detection for NMI happens at the 6502 (via the INTON/INTOFF gating in
        // EconetSocket). The ADLC itself simply reports whether an interrupt condition exists.

        // RIE gates: RDA (SR1b0), S2RQ (SR1b1)
        // Note: FD (SR1b3) is informational only and does NOT participate in IRQ generation.
        // Per MC6854 datasheet: IRQ = (RIE AND (RDA OR S2RQ)) OR (TIE AND (TDRA OR CTS OR TXU))
        bool rx_cause = (cr1_ & CR1_RIE) &&
            ((sr1_ & (SR1_RDA | SR1_S2RQ)) != 0);

        // TIE gates: CTS (SR1b4), TxU (SR1b5), TDRA (SR1b6)
        bool tx_cause = (cr1_ & CR1_TIE) &&
            ((sr1_ & (SR1_CTS | SR1_TXU | SR1_TDRA)) != 0);

        irq_output_ = rx_cause || tx_cause;

        if (irq_output_) {
            sr1_ |= SR1_IRQ;
        } else {
            sr1_ &= ~SR1_IRQ;
        }
    }

    // PSE 4-tier priority filtering for SR2
    uint8_t apply_pse_filter(uint8_t sr2_raw) const {
        // Priority 1 (highest): FV, Abort, FCS Error, DCD, Overrun
        constexpr uint8_t P1_MASK = SR2_FV | SR2_ABT | SR2_ERR | SR2_DCD | SR2_OVRN;
        // Priority 2: Idle
        constexpr uint8_t P2_MASK = SR2_INACTIVE;
        // Priority 3: AP
        constexpr uint8_t P3_MASK = SR2_AP;
        // Priority 4 (lowest): RDA
        constexpr uint8_t P4_MASK = SR2_RDA;

        switch (pse_level_) {
            case 0:
            case 1:
                // Show P1 conditions; suppress P2, P3, P4 if P1 present
                if (sr2_raw & P1_MASK) {
                    return sr2_raw & (P1_MASK);
                }
                // Fall through to P2 if no P1
                if (sr2_raw & P2_MASK) {
                    return sr2_raw & (P1_MASK | P2_MASK);
                }
                if (sr2_raw & P3_MASK) {
                    return sr2_raw & (P1_MASK | P2_MASK | P3_MASK);
                }
                return sr2_raw;  // Show all (P4 = RDA)

            case 2:
                // After first Clear RX Status: show P2+ only
                if (sr2_raw & P2_MASK) {
                    return sr2_raw & (P2_MASK);
                }
                if (sr2_raw & P3_MASK) {
                    return sr2_raw & (P2_MASK | P3_MASK);
                }
                return sr2_raw & (P2_MASK | P3_MASK | P4_MASK);

            case 3:
                // After second Clear RX Status: show P3+ only
                if (sr2_raw & P3_MASK) {
                    return sr2_raw & (P3_MASK);
                }
                return sr2_raw & (P3_MASK | P4_MASK);

            case 4:
                // After third Clear RX Status: show P4 only
                return sr2_raw & P4_MASK;

            default:
                return sr2_raw;
        }
    }

    // --- State ---

    NetworkBackend& backend_;

    // Control registers
    uint8_t cr1_ = CR1_RX_RESET | CR1_TX_RESET;
    uint8_t cr2_ = 0;
    uint8_t cr3_ = 0;
    uint8_t cr4_ = 0;

    // Status registers (derived values, updated by update_status())
    uint8_t sr1_ = 0;
    uint8_t sr2_ = 0;

    // TX FIFO (3 entries, shift-register style)
    std::array<uint16_t, FIFO_SIZE> tx_fifo_ = {};

    // RX FIFO (3 entries, shift-register style)
    std::array<uint16_t, FIFO_SIZE> rx_fifo_ = {};

    // Frame field tracking
    FrameField tx_frame_field_ = FrameField::Idle;
    FrameField rx_frame_field_ = FrameField::Idle;

    // TX frame buffer — accumulates bytes for backend send
    std::vector<uint8_t> tx_frame_buffer_;

    // RX frame buffer — holds received frame for byte-trickle into RX FIFO
    std::vector<uint8_t> rx_frame_buffer_;
    size_t rx_buffer_index_ = 0;

    // IRQ output
    bool irq_output_ = false;

    // Stored status latches (cleared by CPU via Clear Rx/Tx Status)
    bool fd_stored_ = false;     // SR1: Flag Detected
    bool cts_stored_ = false;    // SR1: CTS positive edge
    bool txu_stored_ = false;    // SR1: Transmitter Underrun

    bool fv_stored_ = false;     // SR2: Frame Valid
    bool err_stored_ = false;    // SR2: FCS/CRC Error
    bool abt_stored_ = false;    // SR2: Rx Abort (stored component)
    bool ovrn_stored_ = false;   // SR2: Rx Overrun
    bool idle_stored_ = false;   // SR2: Rx Idle (stored component)
    bool dcd_stored_ = false;    // SR2: DCD (stored positive-edge latch)

    // Edge detection state for dual-nature bits
    bool prev_dcd_input_ = false;
    bool prev_cts_input_ = false;

    // External CTS input (active low: false = clear to send)
    bool cts_input_ = false;

    // PSE priority level: 0 = inactive/initial, 1-4 = priority tiers
    int pse_level_ = 0;

    // Byte trickle timer (counts 2MHz half-cycles between byte transfers)
    int byte_timer_ = 0;
    int byte_period_ = 128;  // Default: 128 half-cycles = 32us per byte (~200kbps)

};

}  // namespace beebium
