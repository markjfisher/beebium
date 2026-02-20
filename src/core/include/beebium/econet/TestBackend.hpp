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

#include <deque>
#include <vector>

namespace beebium {

// Test double for NetworkBackend. Captures sent frames and allows injection of
// receive frames for deterministic testing of the ADLC and four-way handshake.
class TestBackend : public NetworkBackend {
public:
    void send_frame(const NetworkFrame& frame) override {
        sent_network_frames_.push_back(frame);
        sent_raw_frames_.push_back(frame.data);
    }

    std::optional<NetworkFrame> receive_frame() override {
        if (rx_queue_.empty()) {
            return std::nullopt;
        }
        auto frame = std::move(rx_queue_.front());
        rx_queue_.pop_front();
        return frame;
    }

    bool is_connected() const override {
        return connected_;
    }

    // --- Test control: inject frames ---

    // Inject a raw frame into the receive queue (convenience for non-AUN tests).
    void inject_rx_frame(std::vector<uint8_t> data) {
        NetworkFrame frame;
        frame.type = FrameType::RawFrame;
        frame.data = std::move(data);
        rx_queue_.push_back(std::move(frame));
    }

    // Inject a typed AUN frame into the receive queue.
    void inject_rx_network_frame(NetworkFrame frame) {
        rx_queue_.push_back(std::move(frame));
    }

    // Set connection state.
    void set_connected(bool connected) {
        connected_ = connected;
    }

    // --- Test inspection: sent frames ---

    // Raw data payloads from all sent frames (backward-compatible convenience).
    const std::vector<std::vector<uint8_t>>& sent_frames() const {
        return sent_raw_frames_;
    }

    // Full typed frames sent by the ADLC.
    const std::vector<NetworkFrame>& sent_network_frames() const {
        return sent_network_frames_;
    }

    // Number of frames sent.
    size_t sent_frame_count() const {
        return sent_network_frames_.size();
    }

    // Clear sent frame history.
    void clear_sent_frames() {
        sent_network_frames_.clear();
        sent_raw_frames_.clear();
    }

    // Number of frames remaining in the receive queue.
    size_t rx_queue_size() const {
        return rx_queue_.size();
    }

    // --- Test control: flag fill simulation ---

    bool is_receiving_flags() const override { return flags_active_; }

    void set_flags_active(bool active) { flags_active_ = active; }

private:
    std::vector<NetworkFrame> sent_network_frames_;
    std::vector<std::vector<uint8_t>> sent_raw_frames_;
    std::deque<NetworkFrame> rx_queue_;
    bool connected_ = true;
    bool flags_active_ = false;
};

}  // namespace beebium
