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

#include "beebium/econet/PiconetBackend.hpp"

#include "beebium/econet/FourWayHandshake.hpp"
#include "beebium/econet/piconet/Commands.hpp"
#include "beebium/econet/piconet/Constants.hpp"
#include "beebium/econet/piconet/Events.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace beebium {

namespace {

// Mask used to strip the scout's high control bit before placing the
// control byte into a NetworkFrame. Matches FourWayHandshake's
// CTRL_FUNCTION_MASK (0x7F).
constexpr std::uint8_t CTRL_FUNCTION_MASK = 0x7F;

// The Econet scout/data marker bit. FourWayHandshake stores ctrl bytes
// with this stripped (control_byte holds the function code, 0-127).
// On the wire, scouts and broadcasts/immediates need it set so the
// receiving station's ADLC sees the right frame type. PiconetBackend
// restores it before passing the byte to the firmware's TX/BCAST commands.
constexpr std::uint8_t WIRE_CTRL_HIGH_BIT = 0x80;

bool trace_enabled() {
    static const bool on = std::getenv("BEEBIUM_PICONET_TRACE") != nullptr;
    return on;
}

void write_to_serial(piconet::SerialPort& serial, std::string_view line) {
    auto bytes = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(line.data()), line.size()};
    auto result = serial.write(bytes);
    if (result.error || result.bytes != line.size()) {
        if (trace_enabled()) {
            std::cerr << "PiconetBackend: serial write incomplete ("
                      << result.bytes << "/" << line.size()
                      << ", error=" << result.error << ")\n";
        }
    }
}

// Construct a typed Unicast/Immediate NetworkFrame from a Piconet
// RX_TRANSMIT or RX_IMMEDIATE event. Returns nullopt on malformed input
// (scout < 6 bytes or data < 4 bytes).
//
// Wire-format extraction (matches piconet/board/src/econet.c frame layout):
//   scout = [dest_stn, dest_net, src_stn, src_net, ctrl, port, scout_extra...]
//   data  = [dest_stn, dest_net, src_stn, src_net, payload...]
//
// FourWayHandshake's NetworkFrame layout for incoming Unicast/Immediate
// has nf.data = [scout_extra_bytes, data_payload_bytes]; the addressing
// fields capture sender/receiver as separate uint8s.
std::optional<NetworkFrame> make_typed_frame(FrameType type,
                                             const std::vector<std::uint8_t>& scout,
                                             const std::vector<std::uint8_t>& data_frame) {
    if (scout.size() < 6 || data_frame.size() < 4) {
        return std::nullopt;
    }
    NetworkFrame nf;
    nf.type = type;
    nf.dest_stn     = scout[0];
    nf.dest_net     = scout[1];
    nf.src_stn      = scout[2];
    nf.src_net      = scout[3];
    nf.control_byte = static_cast<std::uint8_t>(scout[4] & CTRL_FUNCTION_MASK);
    nf.port         = scout[5];
    // Concatenate scout-extra (after the 6-byte scout header) and data
    // payload (after the 4-byte data header) -- matches FourWayHandshake's
    // outbound packing in handle_tx_data_after_scout_ack().
    nf.data.reserve((scout.size() - 6) + (data_frame.size() - 4));
    nf.data.insert(nf.data.end(), scout.begin() + 6, scout.end());
    nf.data.insert(nf.data.end(), data_frame.begin() + 4, data_frame.end());
    return nf;
}

// Construct a Broadcast NetworkFrame from a Piconet RX_BROADCAST event.
// Wire format: [dest=0xFF, dest_net=0xFF, src_stn, src_net, ctrl, port, payload...]
std::optional<NetworkFrame> make_broadcast_frame(const std::vector<std::uint8_t>& wire) {
    if (wire.size() < 6) return std::nullopt;
    NetworkFrame nf;
    nf.type         = FrameType::Broadcast;
    nf.dest_stn     = wire[0];
    nf.dest_net     = wire[1];
    nf.src_stn      = wire[2];
    nf.src_net      = wire[3];
    nf.control_byte = static_cast<std::uint8_t>(wire[4] & CTRL_FUNCTION_MASK);
    nf.port         = wire[5];
    if (wire.size() > 6) {
        nf.data.assign(wire.begin() + 6, wire.end());
    }
    return nf;
}

// Construct a bare Ack NetworkFrame for FourWayHandshake to consume on
// TX_RESULT OK. FourWayHandshake's handler at handle_incoming() Stage::DataSent
// branch ignores Ack addressing and uses its own saved_* values, so an
// empty Ack suffices to short-circuit the synthetic final-ack timer and
// complete the handshake immediately. Without this, every TX would wait
// for FINAL_ACK_TIMEOUT (5ms) or the watchdog (250ms).
NetworkFrame make_ack() {
    NetworkFrame nf;
    nf.type = FrameType::Ack;
    return nf;
}

}  // namespace

PiconetBackend::PiconetBackend(piconet::PiconetConfig config,
                               std::unique_ptr<piconet::SerialPort> serial)
    : config_(std::move(config)),
      serial_(std::move(serial)) {
    if (trace_enabled()) {
        std::cerr << "PiconetBackend: constructed for device " << config_.device_path
                  << " station=" << static_cast<unsigned>(config_.initial_station) << "\n";
    }

    // If the serial port failed to open, leave the backend disconnected:
    // is_connected() will return false, send_frame()/receive_frame() are
    // no-ops, and the reader thread is never started.
    if (!serial_ || !serial_->is_open()) {
        return;
    }

    // Initial handshake: tell the device our station, then put it in LISTEN
    // mode. Failures are logged via trace but do not abort -- the next
    // operation will observe the failure.
    write_to_serial(*serial_, piconet::format_set_station(config_.initial_station));
    write_to_serial(*serial_, piconet::format_set_mode(piconet::Mode::Listen));

    reader_thread_ = std::thread([this] { reader_loop(); });
}

PiconetBackend::~PiconetBackend() {
    shutdown_.store(true, std::memory_order_relaxed);
    if (serial_) {
        // Closing the serial port unblocks the reader's timed read by
        // returning ReadResult{error=true}, which lets the loop exit
        // promptly even if no bytes were arriving.
        serial_->close();
    }
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
}

void PiconetBackend::reader_loop() {
    std::string line_buffer;
    std::array<std::uint8_t, 512> buf{};

    while (!shutdown_.load(std::memory_order_relaxed)) {
        auto result = serial_->read({buf.data(), buf.size()});
        if (result.error) {
            // Serial closed or hangup: exit the loop. is_open() will return
            // false, propagating to is_connected().
            return;
        }
        if (result.would_block) {
            continue;
        }

        line_buffer.append(reinterpret_cast<const char*>(buf.data()), result.bytes);

        // Drain every complete line currently buffered. The firmware emits
        // events terminated by '\n'; we tolerate a trailing '\r' before it.
        while (true) {
            auto newline = line_buffer.find(piconet::EVENT_TERMINATOR);
            if (newline == std::string::npos) break;

            std::string_view line(line_buffer.data(), newline);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }

            auto event = piconet::parse_event_line(line);
            std::visit([this](auto&& ev) {
                using T = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<T, piconet::RxTransmitEvent>) {
                    if (auto nf = make_typed_frame(FrameType::Unicast, ev.scout, ev.data)) {
                        rx_queue_.try_enqueue(std::move(*nf));
                    }
                } else if constexpr (std::is_same_v<T, piconet::RxImmediateEvent>) {
                    if (auto nf = make_typed_frame(FrameType::Immediate, ev.scout, ev.data)) {
                        rx_queue_.try_enqueue(std::move(*nf));
                    }
                } else if constexpr (std::is_same_v<T, piconet::RxBroadcastEvent>) {
                    if (auto nf = make_broadcast_frame(ev.data)) {
                        rx_queue_.try_enqueue(std::move(*nf));
                    }
                } else if constexpr (std::is_same_v<T, piconet::TxResultEvent>) {
                    // OK: short-circuit the synthetic final-ack timer in
                    // FourWayHandshake (handle_incoming() Stage::DataSent)
                    // by enqueueing a bare Ack. Other codes are dropped --
                    // FourWayHandshake's watchdog times out and resets.
                    if (ev.result == piconet::TxResult::Ok) {
                        rx_queue_.try_enqueue(make_ack());
                    } else if (trace_enabled()) {
                        std::cerr << "PiconetBackend: TX failed: "
                                  << piconet::to_string(ev.result) << "\n";
                    }
                } else if constexpr (std::is_same_v<T, piconet::ErrorEvent>) {
                    if (trace_enabled()) {
                        std::cerr << "PiconetBackend: device ERROR: " << ev.message << "\n";
                    }
                } else if constexpr (std::is_same_v<T, piconet::UnknownEvent>) {
                    if (trace_enabled()) {
                        std::cerr << "PiconetBackend: unknown event line: "
                                  << ev.raw_line << "\n";
                    }
                }
                // StatusEvent, ReplyResultEvent, MonitorEvent: nothing to
                // forward in phase 4. Status will inform diagnostics in
                // phase 5; the others have no consumer.
            }, event);

            line_buffer.erase(0, newline + 1);
        }
    }
}

void PiconetBackend::send_frame(const NetworkFrame& frame) {
    if (!serial_ || !serial_->is_open()) {
        return;  // Disconnected: drop silently. FourWayHandshake's watchdog cleans up.
    }

    switch (frame.type) {
        case FrameType::Unicast: {
            // FourWayHandshake packs nf.data as:
            //   [scout_extra_bytes (count = scout_payload_size(ctrl)), data_payload].
            // Piconet's TX command takes scout_extra and data as separate
            // base64 fields, so we split them here.
            const int extra_size =
                FourWayHandshake::scout_payload_size(frame.control_byte);
            std::span<const std::uint8_t> all{frame.data.data(), frame.data.size()};
            std::span<const std::uint8_t> scout_extra;
            std::span<const std::uint8_t> data = all;
            if (extra_size > 0 &&
                static_cast<std::size_t>(extra_size) <= all.size()) {
                scout_extra = all.subspan(0, extra_size);
                data = all.subspan(extra_size);
            } else if (extra_size > 0 && trace_enabled()) {
                std::cerr << "PiconetBackend: Unicast ctrl 0x" << std::hex
                          << static_cast<int>(frame.control_byte)
                          << " expects " << std::dec << extra_size
                          << " scout-extra bytes but data has only "
                          << all.size() << "; sending all as data field\n";
            }
            write_to_serial(*serial_, piconet::format_tx(
                frame.dest_stn, frame.dest_net,
                static_cast<std::uint8_t>(frame.control_byte | WIRE_CTRL_HIGH_BIT),
                frame.port,
                data, scout_extra));
            break;
        }

        case FrameType::Immediate: {
            // Immediates do not carry scout-extra (FourWayHandshake handles
            // ctrl 0x02-0x05 as Unicast scouts, not Immediates). Send via
            // TX with port=0 and an empty scout_extra field.
            write_to_serial(*serial_, piconet::format_tx(
                frame.dest_stn, frame.dest_net,
                static_cast<std::uint8_t>(frame.control_byte | WIRE_CTRL_HIGH_BIT),
                /*port=*/0,
                std::span<const std::uint8_t>{frame.data.data(), frame.data.size()},
                {}));
            break;
        }

        case FrameType::Broadcast: {
            // Piconet stamps the source station from its own SET_STATION value
            // and the dest is hardcoded 0xFF. The BCAST data field is the
            // bytes that come after the 4-byte address header on the wire,
            // i.e. [ctrl, port, payload...]. FourWayHandshake's nf.data
            // contains only the payload (ctrl/port are in the named fields),
            // so we prepend them here.
            std::vector<std::uint8_t> wire_data;
            wire_data.reserve(2 + frame.data.size());
            wire_data.push_back(static_cast<std::uint8_t>(frame.control_byte | WIRE_CTRL_HIGH_BIT));
            wire_data.push_back(frame.port);
            wire_data.insert(wire_data.end(), frame.data.begin(), frame.data.end());
            write_to_serial(*serial_, piconet::format_bcast(wire_data));
            break;
        }

        case FrameType::Ack:
            // The wire-level four-way handshake has already completed inside
            // Piconet's firmware before the host sees TX_RESULT. Synthesised
            // Acks from FourWayHandshake have nowhere to go.
            break;

        case FrameType::ImmReply:
            // Replies to inbound immediate operations cannot be delivered:
            // Piconet's firmware does not support host-driven in-handshake
            // replies (the REPLY path was abandoned upstream -- see
            // docs/discussion/piconet-feasibility.md, "Immediate Operations
            // Limitation"). Drop silently.
            if (trace_enabled()) {
                std::cerr << "PiconetBackend: dropping ImmReply (Piconet's "
                             "REPLY path is unsupported by firmware)\n";
            }
            break;

        case FrameType::Nack:
            // AUN-only; not used in practice.
            break;

        case FrameType::RawFrame:
            // FourWayHandshake should never hand a RawFrame to a backend
            // operating in aun_mode. Defensive drop.
            if (trace_enabled()) {
                std::cerr << "PiconetBackend: dropping unexpected RawFrame "
                             "(should not occur in aun_mode)\n";
            }
            break;
    }
}

std::optional<NetworkFrame> PiconetBackend::receive_frame() {
    NetworkFrame frame;
    if (rx_queue_.try_dequeue(frame)) {
        return frame;
    }
    return std::nullopt;
}

void PiconetBackend::on_station_id_changed(std::uint8_t new_station_id) {
    if (!serial_ || !serial_->is_open()) {
        return;
    }
    config_.initial_station = new_station_id;  // Keep config in sync for diagnostics.
    write_to_serial(*serial_, piconet::format_set_station(new_station_id));
}

}  // namespace beebium
