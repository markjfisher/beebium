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

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace beebium::piconet {

// Outcome of a non-blocking serial read attempt.
struct ReadResult {
    std::size_t bytes = 0;     // Number of bytes written into the caller's buffer.
    bool would_block = false;  // True if the read returned with no data within the timeout.
    bool error = false;        // True on any unrecoverable I/O error (port closed, hangup, etc.).
};

// Outcome of a serial write attempt.
struct WriteResult {
    std::size_t bytes = 0;  // Number of bytes accepted by the OS.
    bool error = false;     // True if the underlying write failed.
};

// Abstract serial port for the Piconet backend. One pure-abstract interface,
// multiple implementations: PosixSerialPort (production), MockPiconetSerial
// (unit-test scripted I/O), FakePiconetDevice (behavioural model), PtySerialPort
// (PTY-bridged for hardware-parity tests).
//
// Granularity is bytes, not lines. Real serial returns arbitrary chunks; line
// buffering lives in the consumer (PiconetBackend's reader thread).
//
// Threading contract: writes are made only from the emulation thread
// (PiconetBackend::send_frame and PiconetBackend::on_station_id_changed).
// Reads are made only from PiconetBackend's dedicated reader thread. No
// concurrent writes ever occur, so implementations need no internal mutex on
// write(). If a future caller needs to mutate state from another thread (e.g.
// a gRPC RPC handler), the synchronisation belongs at that higher layer (queue
// to the emulation tick), not buried in this interface.
class SerialPort {
public:
    virtual ~SerialPort() = default;

    // Non-blocking read with a short internal timeout (typically 100ms).
    // Returns the number of bytes read into the buffer, or signals
    // would_block if the timeout elapsed without data.
    virtual ReadResult read(std::span<std::uint8_t> buffer) = 0;

    // Synchronous write. Returns the number of bytes accepted by the OS;
    // partial writes are possible on some platforms but unusual at our
    // line lengths.
    virtual WriteResult write(std::span<const std::uint8_t> bytes) = 0;

    // True if the port is open and usable. Goes false on close() or after
    // an unrecoverable I/O error.
    virtual bool is_open() const = 0;

    // Close the port. After close(), is_open() returns false and any
    // blocked read() in progress should return promptly with error=true
    // (this is what allows the reader thread to be joined cleanly during
    // PiconetBackend destruction).
    virtual void close() = 0;

    // OS-level diagnosis recorded if the last open attempt failed.
    // Empty on success. Pure virtual so concrete implementations
    // can't silently lose the error string by inheriting an empty
    // default -- a regression that would surface to the user as a
    // generic "Adapter offline" instead of the actual reason.
    // PiconetBackend::process_pending_reopen reads this to populate
    // the Extension UI Indicator after a user-initiated path change.
    virtual std::string_view open_error() const noexcept = 0;
};

}  // namespace beebium::piconet
