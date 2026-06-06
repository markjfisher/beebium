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

#ifndef BEEBIUM_EXT_RFC2217_CLIENT_ENDPOINT_HPP
#define BEEBIUM_EXT_RFC2217_CLIENT_ENDPOINT_HPP

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
class TcpClientSerialPort;
}

namespace beebium::rfc2217 {

// A SerialPortDevice that bridges the BBC serial port (RS423) to a remote RFC
// 2217 access server (ser2net, a networked FujiNet, pyserial's rfc2217_server).
// Beebium is the Telnet client: it connects out, negotiates the COM-PORT option,
// sets the remote UART's baud, conveys the BBC's RTS via SET-CONTROL, consumes
// modem-state, and tunnels data with IAC IAC escaping.
//
// The threading + bounded-queue + /CTS back-pressure shape is the ip232 endpoint's
// (a connection/reader thread + a writer thread; the emulation-thread methods only
// touch mutex-protected queues + atomics). The transmit queue holds already-framed
// wire bytes fed from three sources -- transmitted data (encode_data), the BBC's
// RTS (SET-CONTROL), and the codec's Telnet negotiation replies -- all serialised
// under tx_mutex_ and drained by the single writer.
class Rfc2217ClientEndpoint final : public beebium::SerialPortDevice {
public:
    struct Options {
        std::string host = "localhost";
        std::uint16_t port = 0;
        std::uint32_t baud = 19200;  // sent to the remote UART (real hardware)
        // Remote framing + handshaking (RFC 2217 payload values, from comport::).
        std::uint8_t data_bits = 8;
        std::uint8_t parity = comport::PARITY_NONE;
        std::uint8_t stop_bits = comport::STOPSIZE_ONE;
        std::uint8_t flow = comport::CONTROL_FLOW_NONE;
        bool dtr = true;
        std::size_t tx_back_pressure = serial::kDefaultTxBackPressure;
        std::function<void()> on_async_state_change;
    };

    explicit Rfc2217ClientEndpoint(Options options);
    ~Rfc2217ClientEndpoint() override;

    Rfc2217ClientEndpoint(const Rfc2217ClientEndpoint&) = delete;
    Rfc2217ClientEndpoint& operator=(const Rfc2217ClientEndpoint&) = delete;

    // --- SerialDataSource (device -> Beeb), emulation thread ---
    bool has_data() override;
    std::uint8_t next_byte() override;

    // --- SerialDataSink (Beeb -> device), emulation thread ---
    void add_byte(std::uint8_t value) override;
    bool accepts_more() override;
    void set_rts(bool rts_asserted) override;

    // --- Diagnostics ---
    bool connected() const { return connected_.load(); }
    bool option_negotiated() const { return negotiated_.load(); }

    struct UiSnapshot {
        std::string host;
        std::uint16_t port = 0;
        std::uint32_t baud = 0;
        bool connected = false;
        std::string last_error;
    };
    UiSnapshot ui_snapshot() const;

private:
    void connection_loop();
    void writer_loop();
    std::shared_ptr<net::TcpClientSerialPort> current_port() const;
    void set_port(std::shared_ptr<net::TcpClientSerialPort> port);
    void set_error(std::string error);
    void notify_state_changed();
    void enqueue_tx(const std::vector<std::uint8_t>& bytes);  // wire bytes
    void process_command(const ComPortCommand& cmd);

    const std::string host_;
    const std::uint16_t port_num_;
    const std::uint32_t baud_;
    const std::uint8_t data_bits_;
    const std::uint8_t parity_;
    const std::uint8_t stop_bits_;
    const std::uint8_t flow_;
    const bool dtr_;
    const std::size_t tx_back_pressure_;
    const std::size_t tx_hard_cap_;

    Rfc2217Codec codec_{Rfc2217Codec::Role::Client};  // reader thread only

    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> negotiated_{false};
    std::atomic<bool> remote_cts_{true};  // modem-state CTS from the server
    std::atomic<bool> rts_asserted_{false};

    mutable std::mutex port_mutex_;
    std::shared_ptr<net::TcpClientSerialPort> port_;

    std::mutex rx_mutex_;
    std::deque<std::uint8_t> rx_;  // decoded data, device -> Beeb

    std::mutex tx_mutex_;
    std::condition_variable tx_cv_;
    std::vector<std::uint8_t> tx_;  // framed wire bytes, Beeb -> device
    std::size_t tx_dropped_ = 0;

    mutable std::mutex ui_mutex_;
    std::string last_error_;
    std::function<void()> on_async_state_change_;

    std::thread connection_thread_;
    std::thread writer_thread_;
};

}  // namespace beebium::rfc2217

#endif  // BEEBIUM_EXT_RFC2217_CLIENT_ENDPOINT_HPP
