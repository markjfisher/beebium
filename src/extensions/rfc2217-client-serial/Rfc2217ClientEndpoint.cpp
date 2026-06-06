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

#include "Rfc2217ClientEndpoint.hpp"

#include <beebium/net/TcpClientSerialPort.hpp>

#include <chrono>
#include <span>
#include <utility>

namespace beebium::rfc2217 {

using namespace std::chrono_literals;

namespace {
constexpr auto kReconnectBackoff = 500ms;
constexpr auto kConnectTimeout = 2s;
}  // namespace

Rfc2217ClientEndpoint::Rfc2217ClientEndpoint(Options options)
    : host_(std::move(options.host)),
      port_num_(options.port),
      baud_(options.baud),
      data_bits_(options.data_bits),
      parity_(options.parity),
      stop_bits_(options.stop_bits),
      flow_(options.flow),
      dtr_(options.dtr),
      tx_back_pressure_(options.tx_back_pressure),
      tx_hard_cap_(options.tx_back_pressure + serial::kTxHardCapMargin),
      on_async_state_change_(std::move(options.on_async_state_change)) {
    connection_thread_ = std::thread([this] { connection_loop(); });
    writer_thread_ = std::thread([this] { writer_loop(); });
}

Rfc2217ClientEndpoint::~Rfc2217ClientEndpoint() {
    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
    }
    tx_cv_.notify_all();
    if (auto port = current_port()) port->close();
    if (connection_thread_.joinable()) connection_thread_.join();
    if (writer_thread_.joinable()) writer_thread_.join();
}

std::shared_ptr<net::TcpClientSerialPort> Rfc2217ClientEndpoint::current_port() const {
    std::lock_guard<std::mutex> lock(port_mutex_);
    return port_;
}

void Rfc2217ClientEndpoint::set_port(std::shared_ptr<net::TcpClientSerialPort> port) {
    std::lock_guard<std::mutex> lock(port_mutex_);
    port_ = std::move(port);
}

void Rfc2217ClientEndpoint::set_error(std::string error) {
    std::lock_guard<std::mutex> lock(ui_mutex_);
    last_error_ = std::move(error);
}

void Rfc2217ClientEndpoint::notify_state_changed() {
    if (on_async_state_change_) on_async_state_change_();
}

void Rfc2217ClientEndpoint::enqueue_tx(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (tx_.size() + bytes.size() > tx_hard_cap_) {
        ++tx_dropped_;
        return;
    }
    tx_.insert(tx_.end(), bytes.begin(), bytes.end());
    tx_cv_.notify_one();
}

void Rfc2217ClientEndpoint::process_command(const ComPortCommand& cmd) {
    // The server replies use the +100 form. Modem-state CTS gates the BBC's
    // transmit (real flow control from the remote UART).
    if (cmd.command == comport::NOTIFY_MODEMSTATE + comport::SERVER_OFFSET &&
        !cmd.value.empty()) {
        remote_cts_.store((cmd.value[0] & comport::MODEMSTATE_CTS) != 0,
                          std::memory_order_release);
    }
    // NOTIFY-LINESTATE carries the remote's break-detect bit. Edge-detect the
    // rising transition and arm one break frame for the BBC's receiver (the
    // BBC sees "a break happened"; the exact duration is not preserved).
    if (cmd.command == comport::NOTIFY_LINESTATE + comport::SERVER_OFFSET &&
        !cmd.value.empty()) {
        const bool break_now = (cmd.value[0] & comport::LINESTATE_BREAK) != 0;
        if (break_now && !remote_break_state_) {
            break_pending_.store(true, std::memory_order_release);
        }
        remote_break_state_ = break_now;
    }
}

void Rfc2217ClientEndpoint::connection_loop() {
    std::vector<std::uint8_t> buf(256);
    std::vector<std::uint8_t> data;
    std::vector<ComPortCommand> cmds;
    std::vector<std::uint8_t> reply;

    while (!stop_.load(std::memory_order_acquire)) {
        std::shared_ptr<net::TcpClientSerialPort> port = current_port();

        if (!port) {
            auto fresh = std::make_shared<net::TcpClientSerialPort>(host_, port_num_,
                                                                    kConnectTimeout);
            if (stop_.load(std::memory_order_acquire)) break;
            if (fresh->is_open()) {
                set_error("");
                set_port(fresh);
                // Fresh Telnet session: reset the codec, then queue the initial
                // negotiation and the configured remote baud.
                codec_.reset();
                negotiated_.store(false, std::memory_order_release);
                remote_cts_.store(true, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lock(tx_mutex_);
                    tx_.clear();
                    auto neg = codec_.initial_negotiation();
                    tx_.insert(tx_.end(), neg.begin(), neg.end());
                    // Configure the remote UART: baud, framing, flow, DTR.
                    codec_.encode_set_baudrate(baud_, tx_);
                    codec_.encode_set_datasize(data_bits_, tx_);
                    codec_.encode_set_parity(parity_, tx_);
                    codec_.encode_set_stopsize(stop_bits_, tx_);
                    codec_.encode_set_control(flow_, tx_);
                    codec_.encode_set_control(
                        dtr_ ? comport::CONTROL_DTR_ON : comport::CONTROL_DTR_OFF, tx_);
                    // Ask the server to report received breaks so they can be
                    // injected into the BBC's receiver.
                    codec_.encode_set_linestate_mask(comport::LINESTATE_BREAK, tx_);
                }
                remote_break_state_ = false;
                connected_.store(true, std::memory_order_release);
                tx_cv_.notify_all();
                notify_state_changed();
            } else {
                set_error(std::string(fresh->open_error()));
                notify_state_changed();
                std::this_thread::sleep_for(kReconnectBackoff);
            }
            continue;
        }

        serial::ReadResult r =
            port->read(std::span<std::uint8_t>(buf.data(), buf.size()));
        if (r.error) {
            port->close();
            set_port(nullptr);
            connected_.store(false, std::memory_order_release);
            negotiated_.store(false, std::memory_order_release);
            notify_state_changed();
            std::this_thread::sleep_for(kReconnectBackoff);
            continue;
        }
        if (r.bytes > 0) {
            data.clear();
            cmds.clear();
            reply.clear();
            codec_.decode(std::span<const std::uint8_t>(buf.data(), r.bytes), data, cmds,
                          reply);
            if (!reply.empty()) enqueue_tx(reply);
            if (codec_.option_negotiated() &&
                !negotiated_.load(std::memory_order_acquire)) {
                negotiated_.store(true, std::memory_order_release);
                notify_state_changed();
            }
            if (!data.empty()) {
                std::lock_guard<std::mutex> lock(rx_mutex_);
                for (std::uint8_t b : data) {
                    if (rx_.size() >= serial::kDefaultRxCapacity) break;
                    rx_.push_back(b);
                }
            }
            for (const auto& cmd : cmds) process_command(cmd);
        }
    }
}

void Rfc2217ClientEndpoint::writer_loop() {
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
                break;  // connection lost mid-batch: drop the remainder
            }
            serial::WriteResult w = port->write(
                std::span<const std::uint8_t>(batch.data() + off, batch.size() - off));
            if (w.error) break;
            off += w.bytes;
            if (off < batch.size()) std::this_thread::sleep_for(2ms);
        }
        batch.clear();
    }
}

bool Rfc2217ClientEndpoint::has_data() {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    return !rx_.empty();
}

std::uint8_t Rfc2217ClientEndpoint::next_byte() {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    std::uint8_t value = rx_.front();
    rx_.pop_front();
    return value;
}

bool Rfc2217ClientEndpoint::take_break() {
    return break_pending_.exchange(false, std::memory_order_acq_rel);
}

void Rfc2217ClientEndpoint::add_byte(std::uint8_t value) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (tx_.size() >= tx_hard_cap_) {
        ++tx_dropped_;
        return;
    }
    const std::uint8_t one[] = {value};
    codec_.encode_data(std::span<const std::uint8_t>(one, 1), tx_);
    tx_cv_.notify_one();
}

bool Rfc2217ClientEndpoint::accepts_more() {
    if (!connected_.load(std::memory_order_acquire) ||
        !remote_cts_.load(std::memory_order_acquire)) {
        return false;  // not connected, or the remote UART says stop
    }
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return tx_.size() < tx_back_pressure_;
}

void Rfc2217ClientEndpoint::set_rts(bool rts_asserted) {
    rts_asserted_.store(rts_asserted, std::memory_order_release);
    std::vector<std::uint8_t> control;
    codec_.encode_set_control(
        rts_asserted ? comport::CONTROL_RTS_ON : comport::CONTROL_RTS_OFF, control);
    enqueue_tx(control);
}

void Rfc2217ClientEndpoint::set_break(bool break_asserted) {
    // Carry the BBC's transmitted break to the remote UART via SET-CONTROL.
    std::vector<std::uint8_t> control;
    codec_.encode_set_control(
        break_asserted ? comport::CONTROL_BREAK_ON : comport::CONTROL_BREAK_OFF,
        control);
    enqueue_tx(control);
}

Rfc2217ClientEndpoint::UiSnapshot Rfc2217ClientEndpoint::ui_snapshot() const {
    UiSnapshot snapshot;
    snapshot.host = host_;
    snapshot.port = port_num_;
    snapshot.baud = baud_;
    snapshot.connected = connected_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        snapshot.last_error = last_error_;
    }
    return snapshot;
}

}  // namespace beebium::rfc2217
