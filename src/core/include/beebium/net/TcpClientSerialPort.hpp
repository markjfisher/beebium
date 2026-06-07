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

#ifndef BEEBIUM_NET_TCP_CLIENT_SERIAL_PORT_HPP
#define BEEBIUM_NET_TCP_CLIENT_SERIAL_PORT_HPP

#include <beebium/serial/HostSerialPort.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace beebium::net {

// An outbound TCP connection presented as a serial::HostSerialPort -- a raw,
// protocol-agnostic byte pipe. The constructor connects to host:port; whether
// it succeeded is reported by is_open() / open_error() (it never throws).
//
// This is the network analogue of PosixSerialPort/Win32SerialPort: the byte
// transport under the host-serial endpoint machinery, with a socket instead of
// a tty. The IP232 and RFC 2217 codecs layer ON TOP of it in their own
// SerialPortDevice endpoints; this class knows nothing about either protocol.
//
// Threading contract (same as HostSerialPort): one writer thread calls write(),
// a reader thread calls read(). close() may be called from any thread and
// promptly unblocks a blocked read() by shutting the socket down (which makes a
// reader parked in select() return). The underlying socket descriptor stays
// valid until the destructor, so close() never races a concurrent select()
// onto a reused descriptor; callers join their I/O threads before destroying
// the port (the host-serial endpoint pattern).
class TcpClientSerialPort final : public serial::HostSerialPort {
public:
    TcpClientSerialPort(
        const std::string& host, uint16_t port,
        std::chrono::milliseconds connect_timeout = std::chrono::seconds(5));
    ~TcpClientSerialPort() override;

    TcpClientSerialPort(const TcpClientSerialPort&) = delete;
    TcpClientSerialPort& operator=(const TcpClientSerialPort&) = delete;

    serial::ReadResult read(std::span<std::uint8_t> buffer) override;
    serial::WriteResult write(std::span<const std::uint8_t> bytes) override;
    bool is_open() const override;
    void close() override;
    std::string_view open_error() const noexcept override;

private:
    void connect(const std::string& host, uint16_t port,
                 std::chrono::milliseconds timeout);

    // socket_t is an int (POSIX) / uintptr (Windows); stored as intptr_t so this
    // header need not pull in <winsock2.h>. -1 is the invalid sentinel on both.
    std::atomic<std::intptr_t> fd_{-1};
    std::atomic<bool> open_{false};
    std::mutex close_mutex_;
    std::string open_error_;
};

}  // namespace beebium::net

#endif  // BEEBIUM_NET_TCP_CLIENT_SERIAL_PORT_HPP
