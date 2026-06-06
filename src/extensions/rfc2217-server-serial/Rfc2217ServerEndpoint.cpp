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

#include "Rfc2217ServerEndpoint.hpp"

#include <beebium/net/TcpServerSerialPort.hpp>

#include <chrono>
#include <span>
#include <utility>

namespace beebium::rfc2217 {

using namespace std::chrono_literals;

Rfc2217ServerEndpoint::Rfc2217ServerEndpoint(Options options)
    : tx_back_pressure_(options.tx_back_pressure),
      tx_hard_cap_(options.tx_back_pressure + serial::kTxHardCapMargin),
      bind_(std::move(options.bind)),
      on_async_state_change_(std::move(options.on_async_state_change)) {
    port_ = std::make_unique<net::TcpServerSerialPort>(bind_, options.port);
    if (!port_->is_open()) {
        open_error_ = std::string(port_->open_error());
    }
    connection_thread_ = std::thread([this] { connection_loop(); });
    writer_thread_ = std::thread([this] { writer_loop(); });
}

Rfc2217ServerEndpoint::~Rfc2217ServerEndpoint() {
    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
    }
    tx_cv_.notify_all();
    if (port_) port_->close();  // unblock the reader parked in accept()/recv()
    if (connection_thread_.joinable()) connection_thread_.join();
    if (writer_thread_.joinable()) writer_thread_.join();
}

void Rfc2217ServerEndpoint::enqueue_tx(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (tx_.size() + bytes.size() > tx_hard_cap_) {
        ++tx_dropped_;
        return;
    }
    tx_.insert(tx_.end(), bytes.begin(), bytes.end());
    tx_cv_.notify_one();
}

void Rfc2217ServerEndpoint::on_client_connect() {
    codec_.reset();
    client_rts_.store(true, std::memory_order_release);
    break_pending_.store(false, std::memory_order_release);
    client_linestate_mask_.store(0, std::memory_order_release);
    std::lock_guard<std::mutex> lock(tx_mutex_);
    tx_.clear();  // a new session: drop anything queued for the previous client
}

void Rfc2217ServerEndpoint::process_command(const ComPortCommand& cmd) {
    using namespace comport;
    // The client (e.g. pySerial) sends a burst of SET-* / SET-*-MASK / PURGE-DATA
    // commands on open and waits for each to be acknowledged. We honour the only
    // line we can map (SET-CONTROL RTS -> the BBC's CTS input) and echo-accept
    // everything else (cosmetic against the emulated UART): the server reply is
    // the same command in the +100 form carrying the requested value.
    if (cmd.command == 0 || cmd.command > PURGE_DATA ||
        cmd.command == NOTIFY_LINESTATE || cmd.command == NOTIFY_MODEMSTATE) {
        return;  // not a client request we acknowledge
    }
    if (cmd.command == SET_CONTROL && !cmd.value.empty()) {
        if (cmd.value[0] == CONTROL_RTS_ON) {
            client_rts_.store(true, std::memory_order_release);
        } else if (cmd.value[0] == CONTROL_RTS_OFF) {
            client_rts_.store(false, std::memory_order_release);
        } else if (cmd.value[0] == CONTROL_BREAK_ON) {
            // The client is driving a break: arm one break frame for the BBC's
            // receiver (the BBC sees "a break happened"; on the next BREAK-OFF
            // nothing more is injected).
            break_pending_.store(true, std::memory_order_release);
        }
    }
    if (cmd.command == SET_LINESTATE_MASK && !cmd.value.empty()) {
        // Remember which line-state bits the client wants reported, so the BBC's
        // break is only sent on as NOTIFY-LINESTATE when it was requested.
        client_linestate_mask_.store(cmd.value[0], std::memory_order_release);
    }
    std::vector<std::uint8_t> ack;
    codec_.encode_subneg(static_cast<std::uint8_t>(cmd.command + SERVER_OFFSET),
                         std::span<const std::uint8_t>(cmd.value.data(), cmd.value.size()),
                         ack);
    enqueue_tx(ack);
}

void Rfc2217ServerEndpoint::connection_loop() {
    std::vector<std::uint8_t> buf(256);
    std::vector<std::uint8_t> data, reply;
    std::vector<ComPortCommand> cmds;

    while (!stop_.load(std::memory_order_acquire)) {
        serial::ReadResult r =
            port_->read(std::span<std::uint8_t>(buf.data(), buf.size()));

        // Track client connect/disconnect transitions (read() drives accept).
        const bool conn = port_->is_connected();
        if (conn != connected_.load(std::memory_order_acquire)) {
            if (conn) on_client_connect();
            connected_.store(conn, std::memory_order_release);
            tx_cv_.notify_all();
            if (on_async_state_change_) on_async_state_change_();
        }

        if (r.error) break;  // listener dead
        if (r.bytes > 0) {
            data.clear();
            reply.clear();
            cmds.clear();
            codec_.decode(std::span<const std::uint8_t>(buf.data(), r.bytes), data, cmds,
                          reply);
            if (!reply.empty()) enqueue_tx(reply);
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

void Rfc2217ServerEndpoint::writer_loop() {
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
        std::size_t off = 0;
        while (off < batch.size() && !stop_.load(std::memory_order_acquire)) {
            if (!connected_.load(std::memory_order_acquire)) break;
            serial::WriteResult w = port_->write(
                std::span<const std::uint8_t>(batch.data() + off, batch.size() - off));
            if (w.error) break;
            off += w.bytes;
            if (off < batch.size()) std::this_thread::sleep_for(2ms);
        }
        batch.clear();
    }
}

bool Rfc2217ServerEndpoint::has_data() {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    return !rx_.empty();
}

std::uint8_t Rfc2217ServerEndpoint::next_byte() {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    std::uint8_t value = rx_.front();
    rx_.pop_front();
    return value;
}

bool Rfc2217ServerEndpoint::take_break() {
    return break_pending_.exchange(false, std::memory_order_acq_rel);
}

void Rfc2217ServerEndpoint::add_byte(std::uint8_t value) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (tx_.size() >= tx_hard_cap_) {
        ++tx_dropped_;
        return;
    }
    const std::uint8_t one[] = {value};
    codec_.encode_data(std::span<const std::uint8_t>(one, 1), tx_);
    tx_cv_.notify_one();
}

bool Rfc2217ServerEndpoint::accepts_more() {
    if (!connected_.load(std::memory_order_acquire) ||
        !client_rts_.load(std::memory_order_acquire)) {
        return false;  // no client, or the client de-asserted RTS (BBC /CTS)
    }
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return tx_.size() < tx_back_pressure_;
}

void Rfc2217ServerEndpoint::set_rts(bool rts_asserted) {
    // The BBC's RTS output appears to the client as the server's CTS modem line.
    std::vector<std::uint8_t> notify;
    codec_.encode_notify_modemstate(rts_asserted ? comport::MODEMSTATE_CTS : 0, notify);
    enqueue_tx(notify);
}

void Rfc2217ServerEndpoint::set_break(bool break_asserted) {
    // Report the BBC's transmitted break to the client as NOTIFY-LINESTATE, but
    // only if the client asked to be notified of the break bit.
    if ((client_linestate_mask_.load(std::memory_order_acquire) &
         comport::LINESTATE_BREAK) == 0) {
        return;
    }
    std::vector<std::uint8_t> notify;
    codec_.encode_notify_linestate(
        break_asserted ? comport::LINESTATE_BREAK : 0, notify);
    enqueue_tx(notify);
}

bool Rfc2217ServerEndpoint::is_listening() const {
    return port_ && port_->is_open();
}

std::uint16_t Rfc2217ServerEndpoint::local_port() const {
    return port_ ? port_->local_port() : 0;
}

Rfc2217ServerEndpoint::UiSnapshot Rfc2217ServerEndpoint::ui_snapshot() const {
    UiSnapshot snapshot;
    snapshot.bind = bind_;
    snapshot.port = local_port();
    snapshot.listening = is_listening();
    snapshot.connected = connected_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        snapshot.last_error = open_error_;
    }
    return snapshot;
}

}  // namespace beebium::rfc2217
