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

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

namespace {

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

}  // namespace

PiconetBackend::PiconetBackend(piconet::PiconetConfig config,
                               std::unique_ptr<piconet::SerialPort> serial)
    : config_(std::move(config)),
      serial_(std::move(serial)) {
    if (trace_enabled()) {
        std::cerr << "PiconetBackend: constructed for device " << config_.device_path << "\n";
    }
}

PiconetBackend::~PiconetBackend() = default;

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
                frame.control_byte, frame.port,
                data, scout_extra));
            break;
        }

        case FrameType::Immediate: {
            // Immediates do not carry scout-extra (FourWayHandshake handles
            // ctrl 0x02-0x05 as Unicast scouts, not Immediates). Send via
            // TX with port=0 and an empty scout_extra field.
            write_to_serial(*serial_, piconet::format_tx(
                frame.dest_stn, frame.dest_net,
                frame.control_byte, /*port=*/0,
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
            wire_data.push_back(frame.control_byte);
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

}  // namespace beebium
