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
// receive frames for deterministic testing of the ADLC.
class TestBackend : public NetworkBackend {
public:
    void send(const std::vector<uint8_t>& frame) override {
        sent_frames_.push_back(frame);
    }

    std::optional<std::vector<uint8_t>> receive() override {
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

    // Test control: inject a frame into the receive queue.
    void inject_rx_frame(std::vector<uint8_t> frame) {
        rx_queue_.push_back(std::move(frame));
    }

    // Test control: set connection state.
    void set_connected(bool connected) {
        connected_ = connected;
    }

    // Test inspection: frames sent by the ADLC.
    const std::vector<std::vector<uint8_t>>& sent_frames() const {
        return sent_frames_;
    }

    // Test inspection: number of frames sent.
    size_t sent_frame_count() const {
        return sent_frames_.size();
    }

    // Test inspection: clear sent frame history.
    void clear_sent_frames() {
        sent_frames_.clear();
    }

private:
    std::vector<std::vector<uint8_t>> sent_frames_;
    std::deque<std::vector<uint8_t>> rx_queue_;
    bool connected_ = true;
};

}  // namespace beebium
