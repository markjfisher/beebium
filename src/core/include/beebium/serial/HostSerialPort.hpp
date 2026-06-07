// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

namespace beebium::serial {

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

// Abstract byte-granular host serial port.
//
// One pure-abstract interface, multiple implementations: PosixSerialPort /
// Win32SerialPort (open an existing device path), PtyMaster (create a
// pseudo-terminal and own the master end), plus test doubles. The Piconet
// transport reuses this interface (see the shim headers under
// include/beebium/econet/piconet/).
//
// Granularity is bytes, not lines: real serial returns arbitrary chunks.
//
// Threading contract: a single owner thread performs writes; reads may be made
// from a dedicated reader thread. No concurrent writes occur, so
// implementations need no internal write mutex. Higher layers that mutate from
// other threads must serialise themselves.
class HostSerialPort {
public:
    virtual ~HostSerialPort() = default;

    // Non-blocking read with a short internal timeout (typically 100ms).
    // Returns the number of bytes read, or signals would_block on timeout.
    virtual ReadResult read(std::span<std::uint8_t> buffer) = 0;

    // Synchronous write. Returns the number of bytes accepted by the OS.
    virtual WriteResult write(std::span<const std::uint8_t> bytes) = 0;

    // True if the port is open and usable. Goes false on close() or after an
    // unrecoverable I/O error.
    virtual bool is_open() const = 0;

    // Close the port. After close(), is_open() returns false and any blocked
    // read() in progress returns promptly with error=true.
    virtual void close() = 0;

    // Drive (assert true / clear false) a serial BREAK condition on the line: the
    // TX line is held in the space state until cleared. Default: no-op (pseudo-
    // terminals and test doubles have no real line to break). Real device ports
    // override this. Called on the writer thread, like write().
    virtual void set_break(bool /*asserted*/) {}

    // OS-level diagnosis recorded if the last open attempt failed. Empty on
    // success.
    virtual std::string_view open_error() const noexcept = 0;
};

}  // namespace beebium::serial
