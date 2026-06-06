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

#ifndef BEEBIUM_NET_TCP_SERVER_SERIAL_PORT_HPP
#define BEEBIUM_NET_TCP_SERVER_SERIAL_PORT_HPP

#include <beebium/serial/HostSerialPort.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace beebium::net {

// A listening TCP socket presented as a serial::HostSerialPort -- the accept-one
// analogue of TcpClientSerialPort. It binds `[bind_addr]:port` and serves a
// SINGLE client at a time (a serial port is one cable): the first connection is
// accepted and bridged; further connections are accepted-then-closed (rejected).
// When the client disconnects the listener keeps serving and accepts the next.
//
// is_open() reflects the *listener* (it stays open across client churn);
// is_connected() reports whether a client is currently attached. read() drives
// accept + recv; write() goes to the connected client (or is discarded when none
// is attached). The RFC 2217 server endpoint layers the Telnet codec on top.
//
// Threading: a reader thread calls read() (accept + recv) and a writer thread
// calls write() (send); concurrent recv+send on the one client socket is safe,
// and the client descriptor's lifecycle is guarded so an accept/drop never races
// the writer onto a stale descriptor.
class TcpServerSerialPort final : public serial::HostSerialPort {
public:
    TcpServerSerialPort(const std::string& bind_addr, std::uint16_t port);
    ~TcpServerSerialPort() override;

    TcpServerSerialPort(const TcpServerSerialPort&) = delete;
    TcpServerSerialPort& operator=(const TcpServerSerialPort&) = delete;

    serial::ReadResult read(std::span<std::uint8_t> buffer) override;
    serial::WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override;  // the listener is bound and serving
    void close() override;
    std::string_view open_error() const noexcept override;

    // Server-specific: a client is currently connected.
    bool is_connected() const;

    // The actual bound TCP port (resolves an ephemeral port=0 bind). 0 if not open.
    std::uint16_t local_port() const;

private:
    void listen_on(const std::string& bind_addr, std::uint16_t port);
    void drop_client(std::intptr_t fd_copy);  // close iff still the current client

    std::atomic<std::intptr_t> listen_fd_{-1};
    std::atomic<bool> open_{false};
    mutable std::mutex client_mutex_;
    std::intptr_t client_fd_ = -1;  // guarded by client_mutex_
    std::string open_error_;
};

}  // namespace beebium::net

#endif  // BEEBIUM_NET_TCP_SERVER_SERIAL_PORT_HPP
