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

#include "FakePiconetDevice.hpp"

#include "PiconetWireFormat.hpp"
#include "beebium/econet/piconet/Base64.hpp"
#include "beebium/econet/piconet/Constants.hpp"

#include <algorithm>
#include <cstring>

namespace beebium::piconet::test {

FakePiconetDevice::FakePiconetDevice() = default;

ReadResult FakePiconetDevice::read(std::span<std::uint8_t> buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) {
        return ReadResult{0, false, true};
    }
    if (outgoing_.empty()) {
        return ReadResult{0, true, false};
    }
    const std::size_t to_copy = std::min(buffer.size(), outgoing_.size());
    for (std::size_t i = 0; i < to_copy; ++i) {
        buffer[i] = outgoing_.front();
        outgoing_.pop_front();
    }
    return ReadResult{to_copy, false, false};
}

WriteResult FakePiconetDevice::write(std::span<const std::uint8_t> bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) {
        return WriteResult{0, true};
    }
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

bool FakePiconetDevice::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

void FakePiconetDevice::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
}

// ---- Test control ----

void FakePiconetDevice::set_firmware_version(std::string version) {
    std::lock_guard<std::mutex> lock(mutex_);
    firmware_version_ = std::move(version);
}

void FakePiconetDevice::set_next_tx_result(TxResult result, bool sticky) {
    std::lock_guard<std::mutex> lock(mutex_);
    next_tx_result_ = result;
    tx_result_sticky_ = sticky;
}

// ---- Test inspection ----

std::uint8_t FakePiconetDevice::station() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_;
}

Mode FakePiconetDevice::mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

std::size_t FakePiconetDevice::tx_command_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tx_command_count_;
}

std::uint8_t FakePiconetDevice::last_tx_dest_stn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tx_dest_stn_;
}

std::uint8_t FakePiconetDevice::last_tx_dest_net() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tx_dest_net_;
}

std::uint8_t FakePiconetDevice::last_tx_ctrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tx_ctrl_;
}

std::uint8_t FakePiconetDevice::last_tx_port() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tx_port_;
}

std::vector<std::uint8_t> FakePiconetDevice::last_tx_data() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tx_data_;
}

std::vector<std::uint8_t> FakePiconetDevice::last_tx_scout_extra() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tx_scout_extra_;
}

std::vector<std::uint8_t> FakePiconetDevice::last_bcast_data() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_bcast_data_;
}

std::size_t FakePiconetDevice::bcast_command_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bcast_command_count_;
}

std::size_t FakePiconetDevice::machine_peek_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return machine_peek_count_;
}

// ---- Inbound injection ----

void FakePiconetDevice::inject_inbound_unicast(std::uint8_t src_stn, std::uint8_t src_net,
                                               std::uint8_t ctrl, std::uint8_t port,
                                               std::vector<std::uint8_t> scout_extra,
                                               std::vector<std::uint8_t> data) {
    std::lock_guard<std::mutex> lock(mutex_);
    inject_inbound_unicast_to(station_, /*dest_net=*/0,
                              src_stn, src_net, ctrl, port,
                              std::move(scout_extra), std::move(data));
}

void FakePiconetDevice::inject_inbound_unicast_to(std::uint8_t dest_stn, std::uint8_t dest_net,
                                                  std::uint8_t src_stn, std::uint8_t src_net,
                                                  std::uint8_t ctrl, std::uint8_t port,
                                                  std::vector<std::uint8_t> scout_extra,
                                                  std::vector<std::uint8_t> data) {
    // Caller may already hold mutex_ (when called from inject_inbound_unicast),
    // so this body must not re-lock. It's only callable through that
    // wrapper or by external code that takes the lock itself; document
    // that contract.
    //
    // STOP mode: no events delivered (piconet.c lines 357-359).
    if (mode_ == Mode::Stop) return;

    // LISTEN mode: filter by station address. MONITOR mode would emit
    // a MONITOR event, which we don't bother modelling here -- the
    // PiconetBackend doesn't consume MONITOR.
    if (mode_ == Mode::Listen && dest_stn != station_ && dest_stn != BROADCAST_ADDR) {
        return;
    }

    auto scout_wire = build_scout_wire(dest_stn, dest_net, src_stn, src_net,
                                       ctrl, port,
                                       scout_extra.data(), scout_extra.size());
    auto data_wire  = build_data_wire(dest_stn, dest_net, src_stn, src_net,
                                      data.data(), data.size());
    enqueue_event_locked("RX_TRANSMIT " + encode_base64(scout_wire) + " " +
                         encode_base64(data_wire));
}

void FakePiconetDevice::inject_inbound_broadcast(std::uint8_t src_stn, std::uint8_t src_net,
                                                 std::uint8_t ctrl, std::uint8_t port,
                                                 std::vector<std::uint8_t> payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == Mode::Stop) return;

    // Wire format: [dest=0xFF, dest_net=0xFF, src_stn, src_net, ctrl, port, payload...]
    // Size to the final length and fill by index (no realloc path for GCC's
    // optimiser to mis-analyse as -Wfree-nonheap-object).
    std::vector<std::uint8_t> wire(6 + payload.size());
    wire[0] = BROADCAST_ADDR;
    wire[1] = BROADCAST_ADDR;
    wire[2] = src_stn;
    wire[3] = src_net;
    wire[4] = static_cast<std::uint8_t>(ctrl | 0x80);
    wire[5] = port;
    if (!payload.empty()) {
        std::memcpy(wire.data() + 6, payload.data(), payload.size());
    }
    enqueue_event_locked("RX_BROADCAST " + encode_base64(wire));
}

void FakePiconetDevice::inject_inbound_immediate(std::uint8_t src_stn, std::uint8_t src_net,
                                                 std::uint8_t ctrl,
                                                 std::vector<std::uint8_t> data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == Mode::Stop) return;

    // MachinePeek is auto-replied inline by the firmware -- no event reaches
    // the host. We track the count for test inspection. Source:
    // piconet/board/src/econet.c lines 603-627.
    if (ctrl == MACHINE_PEEK_CTRL) {
        ++machine_peek_count_;
        return;
    }

    auto scout_wire = build_scout_wire(station_, /*dest_net=*/0,
                                       src_stn, src_net, ctrl, /*port=*/0,
                                       /*extra_ptr=*/nullptr, /*extra_len=*/0);
    auto data_wire  = build_data_wire(station_, /*dest_net=*/0,
                                      src_stn, src_net,
                                      data.data(), data.size());
    enqueue_event_locked("RX_IMMEDIATE " + encode_base64(scout_wire) + " " +
                         encode_base64(data_wire));
}

// ---- Internal: command processing ----

void FakePiconetDevice::process_command_locked(std::string_view line) {
    if (line.empty()) return;

    // Tokenise on space.
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
        handle_status_locked();
        return;
    }

    if (tag == "RESTART") {
        // Firmware: ADLC reset (piconet.c line 308). We don't model the ADLC,
        // but we reset to firmware power-on defaults so test code can use
        // RESTART as a clean-slate hook.
        mode_     = Mode::Stop;
        station_  = DEFAULT_STATION;
        outgoing_.clear();
        return;
    }

    if (tag == "SET_MODE" && tokens.size() == 2) {
        // Source: piconet/board/src/piconet.c lines 446-459.
        if (tokens[1] == "STOP")    mode_ = Mode::Stop;
        else if (tokens[1] == "LISTEN")  mode_ = Mode::Listen;
        else if (tokens[1] == "MONITOR") mode_ = Mode::Monitor;
        else enqueue_event_locked("ERROR WHAT??");  // Matches firmware line 495.
        return;
    }

    if (tag == "SET_STATION" && tokens.size() == 2) {
        // Source: piconet/board/src/piconet.c lines 460-472.
        try {
            int v = std::stoi(std::string(tokens[1]));
            if (v < 0 || v > 0xFF) {
                enqueue_event_locked("ERROR WHAT??");
                return;
            }
            station_ = static_cast<std::uint8_t>(v);
        } catch (...) {
            enqueue_event_locked("ERROR WHAT??");
        }
        return;
    }

    if (tag == "TX" && tokens.size() >= 6) {
        // Source: piconet/board/src/piconet.c lines 473-480.
        try {
            last_tx_dest_stn_ = static_cast<std::uint8_t>(std::stoi(std::string(tokens[1])));
            last_tx_dest_net_ = static_cast<std::uint8_t>(std::stoi(std::string(tokens[2])));
            last_tx_ctrl_     = static_cast<std::uint8_t>(std::stoi(std::string(tokens[3])));
            last_tx_port_     = static_cast<std::uint8_t>(std::stoi(std::string(tokens[4])));
            auto data_decoded = decode_base64(tokens[5]);
            last_tx_data_     = data_decoded.value_or(std::vector<std::uint8_t>{});
            if (tokens.size() >= 7) {
                auto extra_decoded = decode_base64(tokens[6]);
                last_tx_scout_extra_ = extra_decoded.value_or(std::vector<std::uint8_t>{});
            } else {
                last_tx_scout_extra_.clear();
            }
            ++tx_command_count_;
            // Emit TX_RESULT with the configured outcome.
            enqueue_event_locked("TX_RESULT " + std::string(to_string(next_tx_result_)));
            if (!tx_result_sticky_) {
                next_tx_result_ = TxResult::Ok;
            }
        } catch (...) {
            enqueue_event_locked("ERROR WHAT??");
        }
        return;
    }

    if (tag == "BCAST" && tokens.size() == 2) {
        // Source: piconet/board/src/piconet.c lines 481-483.
        auto data_decoded = decode_base64(tokens[1]);
        last_bcast_data_ = data_decoded.value_or(std::vector<std::uint8_t>{});
        ++bcast_command_count_;
        enqueue_event_locked("TX_RESULT " + std::string(to_string(next_tx_result_)));
        if (!tx_result_sticky_) {
            next_tx_result_ = TxResult::Ok;
        }
        return;
    }

    // Unknown / malformed command. Source: piconet.c line 495.
    enqueue_event_locked("ERROR WHAT??");
}

void FakePiconetDevice::enqueue_event_locked(std::string_view line) {
    for (char c : line) outgoing_.push_back(static_cast<std::uint8_t>(c));
    outgoing_.push_back(static_cast<std::uint8_t>(EVENT_TERMINATOR));
}

void FakePiconetDevice::handle_status_locked() {
    // STATUS <maj.min.patch> <station_dec> <sr1_hex2> <mode_dec>
    // Source: piconet/board/src/piconet.c lines 189-196.
    // SR1 is a real ADLC register on hardware; the fake doesn't model the
    // ADLC, so we emit 0x00.
    std::string line = "STATUS " + firmware_version_ + " " +
                       std::to_string(static_cast<unsigned>(station_)) + " " +
                       hex2(0x00) + " " +
                       std::to_string(static_cast<int>(mode_));
    enqueue_event_locked(line);
}

}  // namespace beebium::piconet::test
