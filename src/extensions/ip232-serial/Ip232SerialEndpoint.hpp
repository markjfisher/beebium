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

#ifndef BEEBIUM_EXT_IP232_SERIAL_ENDPOINT_HPP
#define BEEBIUM_EXT_IP232_SERIAL_ENDPOINT_HPP

#include "Ip232Codec.hpp"

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

namespace beebium::ip232 {

// A SerialPortDevice that bridges the BBC serial port (RS423) to a tcpser-style
// IP232 server over TCP. It owns a net::TcpClientSerialPort (the raw byte pipe)
// and runs the IP232 escape codec on top, so the emulation thread never touches
// the socket: a connection thread owns the socket lifecycle + reads, and a
// writer thread drains transmitted bytes. The emulation-thread methods
// (has_data/next_byte/add_byte/accepts_more/set_rts) only touch mutex-protected
// queues and atomics, upholding the no-external-peer-stalls-the-emulator
// invariant.
//
// Two modes:
//   ip232 : persistent connection; 0xFF-escaped framing; RTS conveyed as an
//           0xFF escape (when handshake is on).
//   raw   : a pure byte pipe; the TCP connection follows the BBC's RTS line
//           (connect on assert, disconnect on deassert).
//
// Connection state drives the only inbound control line the RS423 connector
// has: while not connected, accepts_more() returns false, so the Serial ULA
// asserts the ACIA's /CTS and the guest's transmit loop stalls cleanly rather
// than transmitting into a dead socket. Inbound IP232 DTR signalling has no pin
// on the BBC connector, so it is decoded but informational.
class Ip232SerialEndpoint final : public beebium::SerialPortDevice {
public:
    struct Options {
        std::string host = "localhost";
        std::uint16_t port = 25232;
        bool raw = false;       // false = ip232 mode, true = raw byte pipe
        bool handshake = true;  // convey RTS via the 0xFF escape (ip232 mode)
        std::size_t tx_back_pressure = serial::kDefaultTxBackPressure;
        std::function<void()> on_async_state_change;  // connect/drop -> UI dirty
    };

    explicit Ip232SerialEndpoint(Options options);
    ~Ip232SerialEndpoint() override;

    Ip232SerialEndpoint(const Ip232SerialEndpoint&) = delete;
    Ip232SerialEndpoint& operator=(const Ip232SerialEndpoint&) = delete;

    // --- SerialDataSource (device -> Beeb), emulation thread ---
    bool has_data() override;
    std::uint8_t next_byte() override;

    // --- SerialDataSink (Beeb -> device), emulation thread ---
    void add_byte(std::uint8_t value) override;
    bool accepts_more() override;
    void set_rts(bool rts_asserted) override;

    // --- Diagnostics / UI ---
    bool connected() const { return connected_.load(); }
    std::size_t tx_back_pressure() const { return tx_back_pressure_; }
    std::size_t tx_hard_cap() const { return tx_hard_cap_; }
    std::size_t tx_pending();
    std::size_t tx_dropped();

    struct UiSnapshot {
        std::string host;
        std::uint16_t port = 0;
        std::string mode;  // "ip232" | "raw"
        bool connected = false;
        std::string last_error;
    };
    UiSnapshot ui_snapshot() const;

private:
    void connection_loop();
    void writer_loop();
    bool want_connected() const;
    std::shared_ptr<net::TcpClientSerialPort> current_port() const;
    void set_port(std::shared_ptr<net::TcpClientSerialPort> port);
    void set_error(std::string error);
    void notify_state_changed();
    void notify_io();

    const std::string host_;
    const std::uint16_t port_num_;
    const bool raw_;
    const bool handshake_;
    Ip232Codec codec_;
    const std::size_t tx_back_pressure_;
    const std::size_t tx_hard_cap_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> rts_asserted_{false};

    // The socket lifecycle is owned by the connection thread; the pointer swap is
    // guarded so the writer thread can take a live reference to write through.
    mutable std::mutex port_mutex_;
    std::shared_ptr<net::TcpClientSerialPort> port_;

    std::mutex rx_mutex_;
    std::deque<std::uint8_t> rx_;  // device -> Beeb (decoded data)

    std::mutex tx_mutex_;
    std::condition_variable tx_cv_;
    std::vector<std::uint8_t> tx_;  // Beeb -> device (IP232-encoded wire bytes)
    std::size_t tx_dropped_ = 0;

    // Wakeups for the connection thread (RTS change in raw mode, teardown).
    std::mutex io_mutex_;
    std::condition_variable io_cv_;

    mutable std::mutex ui_mutex_;
    std::string last_error_;
    std::function<void()> on_async_state_change_;

    std::thread connection_thread_;
    std::thread writer_thread_;
};

}  // namespace beebium::ip232

#endif  // BEEBIUM_EXT_IP232_SERIAL_ENDPOINT_HPP
