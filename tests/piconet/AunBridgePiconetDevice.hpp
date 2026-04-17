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

// Phase 8 multi-emulator integration fake.
//
// AunBridgePiconetDevice exposes the Piconet USB-CDC serial protocol on
// its SerialPort interface (so PiconetBackend can drive it just like a
// real device or FakePiconetDevice) and bridges that traffic onto a UDP
// AUN backplane via a composed beebium::AunBackend. The intended use is
// two Beebium instances each with PiconetBackend wrapped around an
// AunBridgePiconetDevice, peered on UDP loopback -- the result is a
// hermetic two-emulator integration test of the entire Piconet stack
// without any real Piconet hardware on either side.
//
// Translation rules:
//
//   PiconetBackend writes:
//     TX <stn> <net> <ctrl> <port> <data_b64> [<scout_extra_b64>]
//        -> NetworkFrame{Unicast, dest=stn/net, src=our_stn/0,
//                        ctrl=ctrl&0x7F, port=port,
//                        data = scout_extra || data_payload}
//        -> AunBackend::send_frame   (UDP fire-and-forget)
//        -> emit "TX_RESULT OK\n" back to PiconetBackend immediately
//     BCAST <data_b64>  where data = [ctrl, port, payload...]
//        -> NetworkFrame{Broadcast, ...}
//        -> AunBackend::send_frame
//        -> emit "TX_RESULT OK\n"
//     SET_STATION/SET_MODE/STATUS/RESTART -- same as FakePiconetDevice.
//
//   AunBackend::receive_frame() poller thread:
//     Unicast   -> emit RX_TRANSMIT <scout_b64> <data_b64>\n
//                  scout = [dst, dst_net, src, src_net, ctrl|0x80, port,
//                           scout_extra...]
//                  data  = [dst, dst_net, src, src_net, payload...]
//     Broadcast -> emit RX_BROADCAST <wire_b64>\n
//                  wire = [0xFF, 0xFF, src, src_net, ctrl|0x80, port,
//                          payload...]
//     Immediate -> emit RX_IMMEDIATE <scout_b64> <data_b64>\n  (if not
//                  MachinePeek; MachinePeek is auto-replied via canned
//                  response, no event)
//     Ack / ImmReply / Nack / RawFrame: dropped (the wire-level
//     handshake was synthesized; PiconetBackend already saw TX_RESULT OK).
//
// Sentinel firmware version: STATUS reports "99.99.99" so tests can
// distinguish "we're talking to the AUN bridge fake" from "we're talking
// to a real device or FakePiconetDevice".

#include "beebium/econet/AunBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"
#include "beebium/econet/piconet/TxResult.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace beebium::piconet::test {

class AunBridgePiconetDevice : public SerialPort {
public:
    // Bind a UDP socket on local_port (0 = OS picks). local_net/local_stn
    // identify this station to the internal AunBackend (used for
    // outgoing AUN headers and for tagging received frames). The
    // initial Piconet station_ defaults to local_stn.
    AunBridgePiconetDevice(std::uint8_t local_net,
                           std::uint8_t local_stn,
                           std::uint16_t local_port = 0);

    ~AunBridgePiconetDevice() override;

    AunBridgePiconetDevice(const AunBridgePiconetDevice&) = delete;
    AunBridgePiconetDevice& operator=(const AunBridgePiconetDevice&) = delete;

    // SerialPort interface
    ReadResult read(std::span<std::uint8_t> buffer) override;
    WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override;
    void close() override;

    // Peer management (same shape as AunBackend::add_peer).
    void add_peer(std::uint8_t net, std::uint8_t stn,
                  std::uint32_t ip_addr_net_byte_order, std::uint16_t port);

    // Local AUN UDP port (useful when local_port=0 was passed to discover
    // the OS-assigned port).
    std::uint16_t local_port() const;

    // Sentinel version that tests can assert against to confirm they're
    // speaking to the bridge rather than a real device.
    static constexpr const char* SENTINEL_FIRMWARE_VERSION = "99.99.99";

private:
    void process_command_locked(std::string_view line);
    void enqueue_event_locked(std::string_view line);
    void poller_loop();
    void handle_tx_locked(std::uint8_t dest_stn, std::uint8_t dest_net,
                          std::uint8_t ctrl, std::uint8_t port,
                          const std::vector<std::uint8_t>& data,
                          const std::vector<std::uint8_t>& scout_extra);
    void handle_bcast_locked(const std::vector<std::uint8_t>& wire_payload);
    void emit_rx_transmit_locked(const NetworkFrame& nf, bool is_immediate);
    void emit_rx_broadcast_locked(const NetworkFrame& nf);

    std::uint8_t local_net_;
    AunBackend aun_;
    mutable std::mutex mutex_;
    bool open_ = true;

    Mode mode_ = Mode::Stop;
    std::uint8_t station_;
    std::string command_buffer_;
    std::deque<std::uint8_t> outgoing_;

    std::atomic<bool> shutdown_{false};
    std::thread poller_;
};

}  // namespace beebium::piconet::test
