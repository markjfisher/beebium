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

#ifndef BEEBIUM_EXT_RFC2217_SERVER_ENDPOINT_HPP
#define BEEBIUM_EXT_RFC2217_SERVER_ENDPOINT_HPP

#include "Rfc2217Codec.hpp"

#include <beebium/serial/SerialBufferLimits.hpp>
#include <beebium/serial/SerialDevice.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beebium::net {
class TcpServerSerialPort;
}

namespace beebium::rfc2217 {

// A SerialPortDevice that exposes the emulated BBC serial port over RFC 2217:
// Beebium is the access server, so any off-the-shelf RFC 2217 client (pyserial
// rfc2217://, ser2net, socat, tio) can connect and be the device on the far end
// of the BBC's serial cable. The standards-based twin of rpc-serial.
//
// Security/policy: bind loopback by default, one client at a time. The client's
// baud/parity requests are cosmetic against the emulated UART (echo-accepted).
// The client's SET-CONTROL RTS drives the BBC's CTS input (flow control); the
// BBC's RTS output is reported back as a NOTIFY-MODEMSTATE CTS bit.
class Rfc2217ServerEndpoint final : public beebium::SerialPortDevice {
public:
    struct Options {
        std::string bind = "127.0.0.1";
        std::uint16_t port = 0;
        std::size_t tx_back_pressure = serial::kDefaultTxBackPressure;
        std::function<void()> on_async_state_change;
    };

    explicit Rfc2217ServerEndpoint(Options options);
    ~Rfc2217ServerEndpoint() override;

    Rfc2217ServerEndpoint(const Rfc2217ServerEndpoint&) = delete;
    Rfc2217ServerEndpoint& operator=(const Rfc2217ServerEndpoint&) = delete;

    // --- SerialDataSource (device -> Beeb), emulation thread ---
    bool has_data() override;
    std::uint8_t next_byte() override;

    // --- SerialDataSink (Beeb -> device), emulation thread ---
    void add_byte(std::uint8_t value) override;
    bool accepts_more() override;
    void set_rts(bool rts_asserted) override;

    // --- Diagnostics ---
    bool is_listening() const;
    bool connected() const { return connected_.load(); }
    std::uint16_t local_port() const;

    struct UiSnapshot {
        std::string bind;
        std::uint16_t port = 0;
        bool listening = false;
        bool connected = false;
        std::string last_error;
    };
    UiSnapshot ui_snapshot() const;

private:
    void connection_loop();
    void writer_loop();
    void on_client_connect();
    void enqueue_tx(const std::vector<std::uint8_t>& bytes);
    void process_command(const ComPortCommand& cmd);

    const std::size_t tx_back_pressure_;
    const std::size_t tx_hard_cap_;
    std::string bind_;
    std::string open_error_;

    std::unique_ptr<net::TcpServerSerialPort> port_;  // persistent listener
    Rfc2217Codec codec_{Rfc2217Codec::Role::Server};  // reader thread only

    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> client_rts_{true};  // client's SET-CONTROL RTS (BBC CTS-in)

    std::mutex rx_mutex_;
    std::deque<std::uint8_t> rx_;

    std::mutex tx_mutex_;
    std::condition_variable tx_cv_;
    std::vector<std::uint8_t> tx_;
    std::size_t tx_dropped_ = 0;

    mutable std::mutex ui_mutex_;
    std::function<void()> on_async_state_change_;

    std::thread connection_thread_;
    std::thread writer_thread_;
};

}  // namespace beebium::rfc2217

#endif  // BEEBIUM_EXT_RFC2217_SERVER_ENDPOINT_HPP
