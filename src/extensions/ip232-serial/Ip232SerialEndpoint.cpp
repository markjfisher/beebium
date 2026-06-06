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

#include "Ip232SerialEndpoint.hpp"

#include <beebium/net/TcpClientSerialPort.hpp>

#include <chrono>
#include <span>
#include <utility>

namespace beebium::ip232 {

using namespace std::chrono_literals;

namespace {
// Delay before re-attempting a connection after a failure or a drop. Long
// enough not to hammer the peer, short enough to recover promptly; teardown and
// RTS changes interrupt the wait, so it never delays shutdown.
constexpr auto kReconnectBackoff = 500ms;
// Connect timeout for one attempt. Bounds worst-case teardown while a connect to
// an unreachable host is in flight (the constructor never blocks on it -- the
// connection thread does).
constexpr auto kConnectTimeout = 2s;
// Idle wait when raw mode is disconnected (RTS low) with nothing to do.
constexpr auto kIdleWait = 200ms;
}  // namespace

Ip232SerialEndpoint::Ip232SerialEndpoint(Options options)
    : host_(std::move(options.host)),
      port_num_(options.port),
      raw_(options.raw),
      handshake_(options.handshake),
      codec_(options.raw),
      tx_back_pressure_(options.tx_back_pressure),
      tx_hard_cap_(options.tx_back_pressure + serial::kTxHardCapMargin),
      on_async_state_change_(std::move(options.on_async_state_change)) {
    connection_thread_ = std::thread([this] { connection_loop(); });
    writer_thread_ = std::thread([this] { writer_loop(); });
}

Ip232SerialEndpoint::~Ip232SerialEndpoint() {
    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
    }
    tx_cv_.notify_all();   // wake the writer
    notify_io();           // wake the connection thread out of a backoff/idle wait
    if (auto port = current_port()) {
        port->close();     // unblock a read() parked in the connection thread
    }
    if (connection_thread_.joinable()) connection_thread_.join();
    if (writer_thread_.joinable()) writer_thread_.join();
}

bool Ip232SerialEndpoint::want_connected() const {
    // ip232 mode keeps a persistent connection; raw mode follows the RTS line.
    return raw_ ? rts_asserted_.load() : true;
}

std::shared_ptr<net::TcpClientSerialPort> Ip232SerialEndpoint::current_port() const {
    std::lock_guard<std::mutex> lock(port_mutex_);
    return port_;
}

void Ip232SerialEndpoint::set_port(std::shared_ptr<net::TcpClientSerialPort> port) {
    std::lock_guard<std::mutex> lock(port_mutex_);
    port_ = std::move(port);
}

void Ip232SerialEndpoint::set_error(std::string error) {
    std::lock_guard<std::mutex> lock(ui_mutex_);
    last_error_ = std::move(error);
}

void Ip232SerialEndpoint::notify_state_changed() {
    if (on_async_state_change_) on_async_state_change_();
}

void Ip232SerialEndpoint::notify_io() {
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
    }
    io_cv_.notify_all();
}

void Ip232SerialEndpoint::connection_loop() {
    std::vector<std::uint8_t> buf(256);
    std::vector<std::uint8_t> data;
    std::vector<DtrEvent> events;

    while (!stop_.load(std::memory_order_acquire)) {
        const bool want = want_connected();
        std::shared_ptr<net::TcpClientSerialPort> port = current_port();

        if (want && !port) {
            auto fresh = std::make_shared<net::TcpClientSerialPort>(host_, port_num_,
                                                                    kConnectTimeout);
            if (stop_.load(std::memory_order_acquire)) break;
            if (fresh->is_open()) {
                set_error("");
                set_port(std::move(fresh));
                connected_.store(true, std::memory_order_release);
                tx_cv_.notify_all();  // a queued transmit can now flow
                notify_state_changed();
            } else {
                set_error(std::string(fresh->open_error()));
                notify_state_changed();
                std::unique_lock<std::mutex> lock(io_mutex_);
                io_cv_.wait_for(lock, kReconnectBackoff,
                                [this] { return stop_.load(std::memory_order_acquire); });
            }
            continue;
        }

        if (!want && port) {
            // raw mode, RTS dropped: tear the connection down.
            port->close();
            set_port(nullptr);
            connected_.store(false, std::memory_order_release);
            notify_state_changed();
            continue;
        }

        if (want && port) {
            serial::ReadResult r =
                port->read(std::span<std::uint8_t>(buf.data(), buf.size()));
            if (r.error) {
                port->close();
                set_port(nullptr);
                connected_.store(false, std::memory_order_release);
                notify_state_changed();
                std::unique_lock<std::mutex> lock(io_mutex_);
                io_cv_.wait_for(lock, kReconnectBackoff,
                                [this] { return stop_.load(std::memory_order_acquire); });
                continue;
            }
            if (r.bytes > 0) {
                data.clear();
                events.clear();
                codec_.decode(std::span<const std::uint8_t>(buf.data(), r.bytes), data,
                              events);
                if (!data.empty()) {
                    std::lock_guard<std::mutex> lock(rx_mutex_);
                    for (std::uint8_t b : data) {
                        if (rx_.size() >= serial::kDefaultRxCapacity) break;  // overrun
                        rx_.push_back(b);
                    }
                }
                // DTR events are informational: the RS423 connector has no DTR/DCD
                // pin, so there is nothing on the BBC side to drive.
            }
            // would_block: read() already waited ~100ms; just loop.
            continue;
        }

        // !want && !port: raw mode idle (RTS low). Wait for an RTS change.
        std::unique_lock<std::mutex> lock(io_mutex_);
        io_cv_.wait_for(lock, kIdleWait,
                        [this] { return stop_.load(std::memory_order_acquire); });
    }
}

void Ip232SerialEndpoint::writer_loop() {
    std::vector<std::uint8_t> batch;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(tx_mutex_);
            tx_cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_acquire) ||
                       (!tx_.empty() && connected_.load(std::memory_order_acquire));
            });
            if (stop_.load(std::memory_order_acquire)) return;
            batch.assign(tx_.begin(), tx_.end());
            tx_.clear();
        }

        std::shared_ptr<net::TcpClientSerialPort> port = current_port();
        std::size_t off = 0;
        while (off < batch.size() && !stop_.load(std::memory_order_acquire)) {
            if (!port || !port->is_open() ||
                !connected_.load(std::memory_order_acquire)) {
                // Connection lost mid-batch: drop the unsent remainder. Bytes to a
                // dead peer are lost as on real hardware; the guest stalls via
                // /CTS (accepts_more() is false while disconnected) until reconnect.
                break;
            }
            serial::WriteResult w = port->write(
                std::span<const std::uint8_t>(batch.data() + off, batch.size() - off));
            if (w.error) break;
            off += w.bytes;
            if (off < batch.size()) {
                std::this_thread::sleep_for(2ms);  // kernel buffer full (EAGAIN)
            }
        }
        batch.clear();
    }
}

bool Ip232SerialEndpoint::has_data() {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    return !rx_.empty();
}

std::uint8_t Ip232SerialEndpoint::next_byte() {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    std::uint8_t value = rx_.front();
    rx_.pop_front();
    return value;
}

void Ip232SerialEndpoint::add_byte(std::uint8_t value) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (tx_.size() >= tx_hard_cap_) {
        ++tx_dropped_;  // writer can't keep up AND the guest is ignoring /CTS
        return;
    }
    codec_.encode_data(value, tx_);  // 1-2 wire bytes
    tx_cv_.notify_one();
}

bool Ip232SerialEndpoint::accepts_more() {
    // Not connected: hold /CTS so the guest does not transmit into a dead socket.
    if (!connected_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return tx_.size() < tx_back_pressure_;
}

void Ip232SerialEndpoint::set_rts(bool rts_asserted) {
    rts_asserted_.store(rts_asserted, std::memory_order_release);
    if (raw_) {
        // The connection follows RTS; wake the connection thread to act on it.
        notify_io();
    } else if (handshake_) {
        // Convey the RTS change to the server as the 0xFF escape, in order with
        // the data stream. RTS signalling is tiny and must not be dropped at the
        // back-pressure mark, only at the absolute hard cap.
        std::lock_guard<std::mutex> lock(tx_mutex_);
        if (tx_.size() + 2 <= tx_hard_cap_) {
            Ip232Codec::encode_rts(rts_asserted, tx_);
            tx_cv_.notify_one();
        }
    }
}

std::size_t Ip232SerialEndpoint::tx_pending() {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return tx_.size();
}

std::size_t Ip232SerialEndpoint::tx_dropped() {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return tx_dropped_;
}

Ip232SerialEndpoint::UiSnapshot Ip232SerialEndpoint::ui_snapshot() const {
    UiSnapshot snapshot;
    snapshot.host = host_;
    snapshot.port = port_num_;
    snapshot.mode = raw_ ? "raw" : "ip232";
    snapshot.connected = connected_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        snapshot.last_error = last_error_;
    }
    return snapshot;
}

}  // namespace beebium::ip232
