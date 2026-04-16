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

// Behavioural model of the Piconet firmware, implementing SerialPort so it
// can be plugged directly into PiconetBackend in unit tests. Each modelled
// behaviour cites the corresponding upstream firmware source location at
// /Users/rjs/Code/piconet/board/src/. Divergences from real-firmware
// behaviour discovered by the [piconet-hardware] contract suite become
// local fixes here.
//
// Threading: PiconetBackend's reader thread calls read(); the emulation
// thread calls write(); test code calls the inject_*/set_* methods from
// the main thread. All three need synchronisation, so this fake uses a
// single internal mutex. (PosixSerialPort doesn't need one because the
// real OS layer mediates -- this fake has to mediate itself.)
//
// Scope: models the behaviours that PiconetBackend exercises (TX, BCAST,
// SET_STATION, SET_MODE, STATUS, inbound RX*, MachinePeek auto-reply).
// Does NOT model: ADLC register state, wire-level timing, REPLY (dead
// upstream), TEST.

#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"
#include "beebium/econet/piconet/TxResult.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace beebium::piconet::test {

class FakePiconetDevice : public SerialPort {
public:
    FakePiconetDevice();
    ~FakePiconetDevice() override = default;

    // ---- SerialPort interface ----

    ReadResult read(std::span<std::uint8_t> buffer) override;
    WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override;
    void close() override;

    // ---- Test control ----

    // Set the firmware version string reported in STATUS responses.
    // Default: "2.0.20" (matches upstream firmware as of writing).
    // Source: piconet/board/src/piconet.c lines 18-20.
    void set_firmware_version(std::string version);

    // Set the TxResult code that the next TX/BCAST command will produce.
    // Default: TxResult::Ok. Reset to Ok after each command unless
    // sticky=true.
    void set_next_tx_result(TxResult result, bool sticky = false);

    // Inject an inbound 4-way handshake transmit, as if a remote station
    // had completed the wire handshake to deliver scout+data. The fake
    // applies station filtering against its current SET_STATION value
    // (in LISTEN mode) and the MachinePeek auto-reply rule before
    // emitting RX_TRANSMIT. dest_stn defaults to the fake's current
    // station, so injected frames target the device by default.
    void inject_inbound_unicast(std::uint8_t src_stn, std::uint8_t src_net,
                                std::uint8_t ctrl, std::uint8_t port,
                                std::vector<std::uint8_t> scout_extra,
                                std::vector<std::uint8_t> data);
    void inject_inbound_unicast_to(std::uint8_t dest_stn, std::uint8_t dest_net,
                                   std::uint8_t src_stn, std::uint8_t src_net,
                                   std::uint8_t ctrl, std::uint8_t port,
                                   std::vector<std::uint8_t> scout_extra,
                                   std::vector<std::uint8_t> data);

    // Inject an inbound broadcast (no addressing filter; LISTEN/MONITOR
    // both deliver). Source: piconet/board/src/econet.c (broadcast path).
    void inject_inbound_broadcast(std::uint8_t src_stn, std::uint8_t src_net,
                                  std::uint8_t ctrl, std::uint8_t port,
                                  std::vector<std::uint8_t> payload);

    // Inject an inbound immediate operation. ctrl 0x88 (MachinePeek) is
    // handled inline by the firmware -- no event is delivered to the
    // host -- so this method drops it on the floor when ctrl == 0x88.
    // Source: piconet/board/src/econet.c lines 603-627.
    void inject_inbound_immediate(std::uint8_t src_stn, std::uint8_t src_net,
                                  std::uint8_t ctrl,
                                  std::vector<std::uint8_t> data);

    // ---- Test inspection ----

    std::uint8_t station() const;
    Mode mode() const;
    std::size_t tx_command_count() const;

    // Most recent TX command's fields. Undefined if tx_command_count()==0.
    std::uint8_t last_tx_dest_stn() const;
    std::uint8_t last_tx_dest_net() const;
    std::uint8_t last_tx_ctrl() const;
    std::uint8_t last_tx_port() const;
    std::vector<std::uint8_t> last_tx_data() const;
    std::vector<std::uint8_t> last_tx_scout_extra() const;

    // Most recent BCAST data (the bytes sent after the BCAST keyword).
    std::vector<std::uint8_t> last_bcast_data() const;
    std::size_t bcast_command_count() const;

    // True if an inbound MachinePeek was seen (and auto-reply suppressed
    // delivery of an event). Useful for verifying that the host did NOT
    // observe such frames.
    std::size_t machine_peek_count() const;

private:
    // All state is guarded by mutex_. Methods ending in _locked require
    // the caller to hold it.
    mutable std::mutex mutex_;

    bool open_ = true;

    // Firmware-modelled state.
    Mode         mode_     = Mode::Stop;   // Power-on default per piconet.c
    std::uint8_t station_  = 0x02;          // Default per econet.c line 97
    std::string  firmware_version_ = "2.0.20";

    // TX outcome control.
    TxResult next_tx_result_  = TxResult::Ok;
    bool     tx_result_sticky_ = false;

    // Bytes the next read() call will deliver.
    std::deque<std::uint8_t> outgoing_;

    // Accumulated bytes from write() until we see '\r' (command terminator).
    std::string command_buffer_;

    // Last-TX inspection state.
    std::size_t                tx_command_count_   = 0;
    std::uint8_t               last_tx_dest_stn_   = 0;
    std::uint8_t               last_tx_dest_net_   = 0;
    std::uint8_t               last_tx_ctrl_       = 0;
    std::uint8_t               last_tx_port_       = 0;
    std::vector<std::uint8_t>  last_tx_data_;
    std::vector<std::uint8_t>  last_tx_scout_extra_;

    // Last-BCAST inspection state.
    std::size_t                bcast_command_count_ = 0;
    std::vector<std::uint8_t>  last_bcast_data_;

    // MachinePeek auto-reply counter.
    std::size_t                machine_peek_count_ = 0;

    void process_command_locked(std::string_view line);
    void enqueue_event_locked(std::string_view line);
    void handle_status_locked();
};

}  // namespace beebium::piconet::test
