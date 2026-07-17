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

#include "AunBridgePiconetDevice.hpp"

#include "PiconetWireFormat.hpp"
#include "beebium/econet/FourWayHandshake.hpp"
#include "beebium/econet/piconet/Base64.hpp"
#include "beebium/econet/piconet/Constants.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace beebium::piconet::test {

namespace {

constexpr std::uint8_t WIRE_CTRL_HIGH_BIT = 0x80;
constexpr std::uint8_t CTRL_FUNCTION_MASK = 0x7F;

}  // namespace

AunBridgePiconetDevice::AunBridgePiconetDevice(std::uint8_t local_net,
                                               std::uint8_t local_stn,
                                               std::uint16_t local_port)
    : local_net_(local_net),
      aun_(local_net, local_stn, local_port),
      station_(local_stn) {
    if (!aun_.is_connected()) {
        open_ = false;
        return;
    }
    poller_ = std::thread([this] { poller_loop(); });
}

AunBridgePiconetDevice::~AunBridgePiconetDevice() {
    shutdown_.store(true, std::memory_order_relaxed);
    if (poller_.joinable()) {
        poller_.join();
    }
}

ReadResult AunBridgePiconetDevice::read(std::span<std::uint8_t> buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return ReadResult{0, false, true};
    if (outgoing_.empty()) return ReadResult{0, true, false};
    const std::size_t to_copy = std::min(buffer.size(), outgoing_.size());
    for (std::size_t i = 0; i < to_copy; ++i) {
        buffer[i] = outgoing_.front();
        outgoing_.pop_front();
    }
    return ReadResult{to_copy, false, false};
}

WriteResult AunBridgePiconetDevice::write(std::span<const std::uint8_t> bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return WriteResult{0, true};
    for (std::uint8_t b : bytes) {
        if (b == COMMAND_TERMINATOR) {
            process_command_locked(command_buffer_);
            command_buffer_.clear();
        } else {
            command_buffer_.push_back(static_cast<char>(b));
        }
    }
    return WriteResult{bytes.size(), false};
}

bool AunBridgePiconetDevice::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

void AunBridgePiconetDevice::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
}

void AunBridgePiconetDevice::add_peer(std::uint8_t net, std::uint8_t stn,
                                      std::uint32_t ip_addr, std::uint16_t port) {
    aun_.add_peer(net, stn, ip_addr, port);
}

std::uint16_t AunBridgePiconetDevice::local_port() const {
    return aun_.local_port();
}

void AunBridgePiconetDevice::process_command_locked(std::string_view line) {
    if (line.empty()) return;

    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ' ') {
            tokens.emplace_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    tokens.emplace_back(line.substr(start));

    auto tag = tokens[0];

    if (tag == "STATUS") {
        std::string ev = "STATUS ";
        ev += SENTINEL_FIRMWARE_VERSION;
        ev += " ";
        ev += std::to_string(static_cast<unsigned>(station_));
        ev += " ";
        ev += hex2(0x00);
        ev += " ";
        ev += std::to_string(static_cast<int>(mode_));
        enqueue_event_locked(ev);
        return;
    }

    if (tag == "RESTART") {
        mode_ = Mode::Stop;
        station_ = DEFAULT_STATION;
        outgoing_.clear();
        return;
    }

    if (tag == "SET_MODE" && tokens.size() == 2) {
        if (tokens[1] == "STOP")    mode_ = Mode::Stop;
        else if (tokens[1] == "LISTEN")  mode_ = Mode::Listen;
        else if (tokens[1] == "MONITOR") mode_ = Mode::Monitor;
        else enqueue_event_locked("ERROR WHAT??");
        return;
    }

    if (tag == "SET_STATION" && tokens.size() == 2) {
        try {
            int v = std::stoi(std::string(tokens[1]));
            if (v < 0 || v > 0xFF) {
                enqueue_event_locked("ERROR WHAT??");
                return;
            }
            station_ = static_cast<std::uint8_t>(v);
            // Note: not propagated to internal AunBackend, which
            // captured local_stn at construction time. For tests this
            // is fine because we set station at construction to match.
        } catch (...) {
            enqueue_event_locked("ERROR WHAT??");
        }
        return;
    }

    if (tag == "TX" && tokens.size() >= 6) {
        try {
            auto dest_stn = static_cast<std::uint8_t>(std::stoi(std::string(tokens[1])));
            auto dest_net = static_cast<std::uint8_t>(std::stoi(std::string(tokens[2])));
            auto ctrl     = static_cast<std::uint8_t>(std::stoi(std::string(tokens[3])));
            auto port     = static_cast<std::uint8_t>(std::stoi(std::string(tokens[4])));
            auto data_decoded = decode_base64(tokens[5]);
            std::vector<std::uint8_t> data = data_decoded.value_or(std::vector<std::uint8_t>{});
            std::vector<std::uint8_t> scout_extra;
            if (tokens.size() >= 7) {
                auto extra = decode_base64(tokens[6]);
                scout_extra = extra.value_or(std::vector<std::uint8_t>{});
            }
            handle_tx_locked(dest_stn, dest_net, ctrl, port, data, scout_extra);
            enqueue_event_locked("TX_RESULT OK");
        } catch (...) {
            enqueue_event_locked("ERROR WHAT??");
        }
        return;
    }

    if (tag == "BCAST" && tokens.size() == 2) {
        auto data_decoded = decode_base64(tokens[1]);
        auto wire_payload = data_decoded.value_or(std::vector<std::uint8_t>{});
        handle_bcast_locked(wire_payload);
        enqueue_event_locked("TX_RESULT OK");
        return;
    }

    enqueue_event_locked("ERROR WHAT??");
}

void AunBridgePiconetDevice::handle_tx_locked(
    std::uint8_t dest_stn, std::uint8_t dest_net,
    std::uint8_t ctrl, std::uint8_t port,
    const std::vector<std::uint8_t>& data,
    const std::vector<std::uint8_t>& scout_extra) {
    NetworkFrame nf;
    nf.type        = (port == 0 && !(ctrl >= (0x82) && ctrl <= (0x85)))
                     ? FrameType::Immediate : FrameType::Unicast;
    nf.dest_stn    = dest_stn;
    nf.dest_net    = dest_net;
    nf.src_stn     = station_;
    nf.src_net     = 0;
    nf.control_byte = static_cast<std::uint8_t>(ctrl & CTRL_FUNCTION_MASK);
    nf.port        = port;
    // FourWayHandshake's convention: nf.data = scout_extra || data_payload.
    // PiconetBackend split them on the way out; we re-concatenate so
    // AunBackend's wire format matches what a normal AunBackend-based
    // Beebium would emit.
    nf.data.reserve(scout_extra.size() + data.size());
    nf.data.insert(nf.data.end(), scout_extra.begin(), scout_extra.end());
    nf.data.insert(nf.data.end(), data.begin(), data.end());

    aun_.send_frame(nf);
}

void AunBridgePiconetDevice::handle_bcast_locked(
    const std::vector<std::uint8_t>& wire_payload) {
    // PiconetBackend's BCAST wire payload is [ctrl|0x80, port, payload...].
    if (wire_payload.size() < 2) return;
    NetworkFrame nf;
    nf.type        = FrameType::Broadcast;
    nf.dest_stn    = 0xFF;
    nf.dest_net    = 0xFF;
    nf.src_stn     = station_;
    nf.src_net     = 0;
    nf.control_byte = static_cast<std::uint8_t>(wire_payload[0] & CTRL_FUNCTION_MASK);
    nf.port        = wire_payload[1];
    if (wire_payload.size() > 2) {
        nf.data.assign(wire_payload.begin() + 2, wire_payload.end());
    }
    aun_.send_frame(nf);
}

void AunBridgePiconetDevice::enqueue_event_locked(std::string_view line) {
    for (char c : line) outgoing_.push_back(static_cast<std::uint8_t>(c));
    outgoing_.push_back(static_cast<std::uint8_t>(EVENT_TERMINATOR));
}

void AunBridgePiconetDevice::poller_loop() {
    using namespace std::chrono_literals;
    while (!shutdown_.load(std::memory_order_relaxed)) {
        // Drain any inbound NetworkFrames from AunBackend and translate to
        // Piconet event lines. Lock for the whole batch so the outgoing
        // queue stays consistent vs concurrent reads.
        bool any = false;
        for (int i = 0; i < 64; ++i) {  // bounded to avoid hogging the lock
            auto frame_opt = aun_.receive_frame();
            if (!frame_opt) break;
            std::lock_guard<std::mutex> lock(mutex_);
            // STOP mode drops everything (matches FakePiconetDevice behaviour
            // and the firmware's piconet.c lines 357-359).
            if (mode_ == Mode::Stop) {
                continue;
            }
            const auto& nf = *frame_opt;
            switch (nf.type) {
                case FrameType::Unicast:
                    emit_rx_transmit_locked(nf, /*is_immediate=*/false);
                    break;
                case FrameType::Immediate:
                    // MachinePeek (ctrl 0x08 post-mask, 0x88 wire) is auto-
                    // replied by real firmware -- we just drop here, matching
                    // FakePiconetDevice's modelling.
                    if ((nf.control_byte & CTRL_FUNCTION_MASK) == (MACHINE_PEEK_CTRL & CTRL_FUNCTION_MASK)) {
                        break;
                    }
                    emit_rx_transmit_locked(nf, /*is_immediate=*/true);
                    break;
                case FrameType::Broadcast:
                    emit_rx_broadcast_locked(nf);
                    break;
                case FrameType::Ack:
                case FrameType::ImmReply:
                case FrameType::Nack:
                case FrameType::RawFrame:
                    // Wire-level handshake was synthesized; PiconetBackend
                    // already saw TX_RESULT OK. Nothing to forward.
                    break;
            }
            any = true;
        }
        if (!any) std::this_thread::sleep_for(2ms);
    }
}

void AunBridgePiconetDevice::emit_rx_transmit_locked(const NetworkFrame& nf,
                                                     bool is_immediate) {
    // Mimic firmware RX_TRANSMIT/RX_IMMEDIATE: scout (with high bit on
    // ctrl) + data frame. Reverse FourWayHandshake's nf.data packing
    // (scout_extra first, then payload).
    int extra_size = FourWayHandshake::scout_payload_size(nf.control_byte);
    if (is_immediate) extra_size = 0;  // Immediates don't carry scout-extra
    if (extra_size < 0) extra_size = 0;
    if (static_cast<std::size_t>(extra_size) > nf.data.size()) extra_size = static_cast<int>(nf.data.size());

    auto scout_wire = build_scout_wire(nf.dest_stn, nf.dest_net,
                                       nf.src_stn,  nf.src_net,
                                       nf.control_byte, nf.port,
                                       nf.data.data(), static_cast<std::size_t>(extra_size));
    auto data_wire  = build_data_wire(nf.dest_stn, nf.dest_net,
                                      nf.src_stn,  nf.src_net,
                                      nf.data.data() + extra_size,
                                      nf.data.size() - extra_size);

    std::string ev = is_immediate ? "RX_IMMEDIATE " : "RX_TRANSMIT ";
    ev += encode_base64(scout_wire);
    ev += " ";
    ev += encode_base64(data_wire);
    enqueue_event_locked(ev);
}

void AunBridgePiconetDevice::emit_rx_broadcast_locked(const NetworkFrame& nf) {
    // Size to the final length and fill by index (no realloc path for GCC's
    // optimiser to mis-analyse as -Wfree-nonheap-object).
    std::vector<std::uint8_t> wire(6 + nf.data.size());
    wire[0] = BROADCAST_ADDR;
    wire[1] = BROADCAST_ADDR;
    wire[2] = nf.src_stn;
    wire[3] = nf.src_net;
    wire[4] = static_cast<std::uint8_t>(nf.control_byte | WIRE_CTRL_HIGH_BIT);
    wire[5] = nf.port;
    if (!nf.data.empty()) {
        std::memcpy(wire.data() + 6, nf.data.data(), nf.data.size());
    }

    std::string ev = "RX_BROADCAST ";
    ev += encode_base64(wire);
    enqueue_event_locked(ev);
}

}  // namespace beebium::piconet::test
