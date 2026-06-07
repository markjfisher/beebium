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

#ifdef _WIN32

#include "beebium/serial/Win32SerialPort.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <iostream>
#include <string>

namespace beebium::serial {

namespace {

// Path normalisation mirrors what users type at the --piconet device_path
// CLI argument. A bare "COM3" must be prefixed with \\.\ for the DOS-device
// namespace so that COM10+ work. Paths already beginning with \\ (UNC or
// \\.\ device namespace, including named-pipe paths used by tests) pass
// through unchanged.
std::string normalise_com_path(const std::string& in) {
    if (in.size() >= 2 && in[0] == '\\' && in[1] == '\\') {
        return in;
    }
    if (in.size() >= 3) {
        char c0 = in[0], c1 = in[1], c2 = in[2];
        bool looks_like_com = (c0 == 'C' || c0 == 'c') &&
                              (c1 == 'O' || c1 == 'o') &&
                              (c2 == 'M' || c2 == 'm');
        if (looks_like_com) {
            return std::string("\\\\.\\") + in;
        }
    }
    return in;
}

std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
        result.data(), needed);
    return result;
}

std::string format_last_error(DWORD err) {
    LPWSTR buffer = nullptr;
    DWORD formatted = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (formatted == 0 || buffer == nullptr) {
        return "error " + std::to_string(err);
    }
    int needed = ::WideCharToMultiByte(
        CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
    std::string result;
    if (needed > 1) {
        result.resize(static_cast<std::size_t>(needed - 1));
        ::WideCharToMultiByte(
            CP_UTF8, 0, buffer, -1, result.data(), needed, nullptr, nullptr);
    }
    ::LocalFree(buffer);
    while (!result.empty() &&
           (result.back() == '\r' || result.back() == '\n' ||
            result.back() == ' ')) {
        result.pop_back();
    }
    if (result.empty()) {
        return "error " + std::to_string(err);
    }
    return result;
}

// Configure a real COM-port handle for raw 115200 8N1. The CALLER must
// guarantee that `handle` is for a true serial device (FILE_TYPE_CHAR);
// see is_comm_handle() and the Win32SerialPort constructor below for
// the gate. Calling any of the Get*/Set*/PurgeComm/EscapeCommFunction
// family on a non-COM handle (a named pipe, regular file, mailslot,
// ...) is documented to return ERROR_INVALID_FUNCTION but **also has
// a kernel-side side effect** on Windows builds where strict handle
// checking or OBJ_PROTECT_CLOSE is in play: the failed comm API
// internally closes a transient kernel object that's invalid in this
// context, NtClose raises EXCEPTION_INVALID_HANDLE, and
// KiUserExceptionDispatcher writes the resulting EXCEPTION_RECORD +
// CONTEXT onto the calling thread's user stack. That stack write
// lands at our saved return-address slot and crashes the function on
// return with an AV at <piconet-base-high16>00000000.
//
// The corruption is invisible to TTD's user-mode memory-write
// tracking, invisible to MSVC ASan, and invisible to hardware data
// watchpoints. Application Verifier's PageHeap "fixes" it only by
// relayout. See docs/discussion/test-grpc-piconet-ui-windows-av.md
// for the full investigation; the practical takeaway is "never call
// any comm API on a handle that isn't a real COM port".
DWORD baud_to_cbr(int baud) {
    switch (baud) {
        case 75:     return CBR_110;     // no CBR_75; nearest legal constant
        case 110:    return CBR_110;
        case 300:    return CBR_300;
        case 600:    return CBR_600;
        case 1200:   return CBR_1200;
        case 2400:   return CBR_2400;
        case 4800:   return CBR_4800;
        case 9600:   return CBR_9600;
        case 19200:  return CBR_19200;
        case 38400:  return CBR_38400;
        case 57600:  return CBR_57600;
        case 115200: return CBR_115200;
        default:     return CBR_115200;
    }
}

void configure_comm(HANDLE handle, const std::string& path, int baud) {
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(handle, &dcb)) {
        DWORD err = ::GetLastError();
        // Kept as a defensive fallback only; the caller's
        // is_comm_handle() gate should already have prevented us from
        // reaching here for non-COM handles, so anything that lands
        // here represents a driver-specific quirk on a real comm
        // device rather than the documented non-COM crash path.
        if (err == ERROR_INVALID_FUNCTION || err == ERROR_INVALID_PARAMETER ||
            err == ERROR_INVALID_HANDLE) {
            return;
        }
        std::cerr << "Win32SerialPort: GetCommState(" << path
                  << ") failed: " << format_last_error(err) << "\n";
        return;
    }

    dcb.BaudRate = baud_to_cbr(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    // Piconet firmware uses USB-CDC; CDC-ACM adapters commonly auto-reset
    // the target MCU when DTR is asserted. PosixSerialPort relies on
    // driver-default state and does not explicitly assert DTR. Match that
    // by explicitly disabling DTR/RTS driver-asserted control here.
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!::SetCommState(handle, &dcb)) {
        std::cerr << "Win32SerialPort: SetCommState(" << path
                  << ") failed: " << format_last_error(::GetLastError())
                  << "\n";
        // Proceed: the handle is still usable for raw read/write in tests.
    }

    // ReadIntervalTimeout = MAXDWORD with the other two zero tells the
    // driver: return immediately from ReadFile with whatever bytes are
    // already buffered, or zero if none. The 100ms read timeout is then
    // enforced by WaitForSingleObject on the OVERLAPPED event in read().
    //
    // Write timeouts bound a stuck peer so write() cannot wedge the
    // emulation thread; the constants roughly match "a full scout+data
    // line should never take longer than this".
    COMMTIMEOUTS ct{};
    ct.ReadIntervalTimeout = MAXDWORD;
    ct.ReadTotalTimeoutConstant = 0;
    ct.ReadTotalTimeoutMultiplier = 0;
    ct.WriteTotalTimeoutConstant = 1000;
    ct.WriteTotalTimeoutMultiplier = 10;
    if (!::SetCommTimeouts(handle, &ct)) {
        DWORD err = ::GetLastError();
        if (err != ERROR_INVALID_FUNCTION && err != ERROR_INVALID_PARAMETER &&
            err != ERROR_INVALID_HANDLE) {
            std::cerr << "Win32SerialPort: SetCommTimeouts(" << path
                      << ") failed: " << format_last_error(err) << "\n";
        }
    }

    // Discard any bytes that the driver buffered before we opened.
    ::PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

// Returns true iff the handle is for a real character-mode device
// (a real COM port). Returns false for named pipes, regular files,
// mailslots, sockets, FILE_TYPE_UNKNOWN, etc.
//
// This is the gate that protects configure_comm() from being entered
// on non-COM handles. GetFileType is documented as a cheap, side-
// effect-free probe that does not internally close any kernel
// objects, so it doesn't trigger the user-stack-corrupting exception
// dispatch path that the Win32 comm APIs do. See configure_comm()
// above for the rationale.
bool is_comm_handle(HANDLE handle) {
    return ::GetFileType(handle) == FILE_TYPE_CHAR;
}

ReadResult map_read_error(DWORD err) {
    if (err == ERROR_OPERATION_ABORTED) {
        // Our own CancelIoEx (either from the 100ms timeout path or from
        // close()). The close() case is handled by the caller re-checking
        // the handle; here we report would_block so the reader loop ticks
        // cleanly.
        return ReadResult{0, true, false};
    }
    // All other errors (ERROR_DEVICE_NOT_CONNECTED, ERROR_DEV_NOT_EXIST,
    // ERROR_IO_DEVICE, ERROR_BAD_COMMAND, ERROR_GEN_FAILURE,
    // ERROR_BROKEN_PIPE, ERROR_HANDLE_EOF, ERROR_INVALID_HANDLE, ...) map
    // to an unrecoverable error -- the reader loop closes the port and
    // exits, matching the PosixSerialPort error path.
    return ReadResult{0, false, true};
}

}  // namespace

static_assert(std::atomic<std::uintptr_t>::is_always_lock_free,
              "Win32SerialPort needs lock-free atomic handle storage");

Win32SerialPort::Win32SerialPort(const std::string& device_path, int baud_rate)
    : device_path_(device_path) {
    std::string qualified = normalise_com_path(device_path);
    std::wstring wide = utf8_to_wide(qualified);
    if (wide.empty()) {
        open_error_ = "path conversion to UTF-16 failed";
        std::cerr << "Win32SerialPort: " << open_error_ << " for "
                  << device_path << "\n";
        return;
    }

    HANDLE handle = ::CreateFileW(
        wide.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,                    // dwShareMode -- exclusive (matches POSIX usage)
        nullptr,              // lpSecurityAttributes
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        open_error_ = format_last_error(err);
        std::cerr << "Win32SerialPort: CreateFile(" << qualified
                  << ") failed: " << open_error_ << "\n";
        return;
    }

    HANDLE read_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE write_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (read_event == nullptr || write_event == nullptr) {
        DWORD err = ::GetLastError();
        open_error_ = std::string("CreateEvent failed: ") + format_last_error(err);
        std::cerr << "Win32SerialPort: " << open_error_ << "\n";
        if (read_event) ::CloseHandle(read_event);
        if (write_event) ::CloseHandle(write_event);
        ::CloseHandle(handle);
        return;
    }
    read_event_raw_ = reinterpret_cast<std::uintptr_t>(read_event);
    write_event_raw_ = reinterpret_cast<std::uintptr_t>(write_event);

    // Only configure as a COM port if the handle is for a real
    // character-mode device. Tests open this class against a named
    // pipe (FakePiconetDeviceOnPipe); the pipe is fine for raw
    // ReadFile/WriteFile/CloseHandle but blows up if the COM-config
    // helpers are invoked on it (see the configure_comm comment for
    // the gory details). PosixSerialPort skips its tcgetattr block
    // for non-tty handles for the same architectural reason.
    if (is_comm_handle(handle)) {
        configure_comm(handle, qualified, baud_rate);
    }

    handle_raw_.store(reinterpret_cast<std::uintptr_t>(handle),
                      std::memory_order_release);
}

Win32SerialPort::~Win32SerialPort() {
    close();
    std::uintptr_t deferred = deferred_close_handle_.exchange(
        kInvalidHandle, std::memory_order_acq_rel);
    if (deferred != kInvalidHandle) {
        ::CloseHandle(reinterpret_cast<HANDLE>(deferred));
    }
    if (read_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(read_event_raw_));
        read_event_raw_ = 0;
    }
    if (write_event_raw_ != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(write_event_raw_));
        write_event_raw_ = 0;
    }
}

bool Win32SerialPort::is_open() const {
    return handle_raw_.load(std::memory_order_acquire) != kInvalidHandle;
}

void Win32SerialPort::close() {
    std::uintptr_t raw = handle_raw_.exchange(kInvalidHandle,
                                              std::memory_order_acq_rel);
    if (raw == kInvalidHandle) {
        return;  // Already closed; idempotent.
    }
    // Record for the destructor to CloseHandle once no reader can
    // possibly still be mid-GetOverlappedResult on the handle.
    // PiconetBackend joins its reader thread before destroying its
    // SerialPort, so the destructor is the safe point.
    deferred_close_handle_.store(raw, std::memory_order_release);
    // Best-effort wake for any blocked reader. If no I/O is pending
    // CancelIoEx returns ERROR_NOT_FOUND, which is fine: the reader's
    // 100ms WaitForSingleObject bounds the latency regardless.
    ::CancelIoEx(reinterpret_cast<HANDLE>(raw), nullptr);
}

void Win32SerialPort::set_break(bool asserted) {
    std::uintptr_t raw = handle_raw_.load(std::memory_order_acquire);
    if (raw == kInvalidHandle) {
        return;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(raw);
    // SetCommBreak starts a continuous break; ClearCommBreak ends it.
    if (asserted) {
        ::SetCommBreak(handle);
    } else {
        ::ClearCommBreak(handle);
    }
}

ReadResult Win32SerialPort::read(std::span<std::uint8_t> buffer) {
    std::uintptr_t raw = handle_raw_.load(std::memory_order_acquire);
    if (raw == kInvalidHandle) {
        return ReadResult{0, false, true};
    }
    HANDLE handle = reinterpret_cast<HANDLE>(raw);
    HANDLE event = reinterpret_cast<HANDLE>(read_event_raw_);

    OVERLAPPED ov{};
    ::ResetEvent(event);
    ov.hEvent = event;

    DWORD bytes = 0;
    BOOL ok = ::ReadFile(handle, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &bytes, &ov);
    if (ok) {
        // Synchronous completion. With ReadIntervalTimeout=MAXDWORD
        // + TotalTimeout=0 a COM port returns immediately with
        // whatever bytes are buffered, or zero. Zero bytes is a
        // timeout, not EOF, for a COM device.
        if (bytes == 0) {
            return ReadResult{0, true, false};
        }
        return ReadResult{static_cast<std::size_t>(bytes), false, false};
    }

    DWORD err = ::GetLastError();
    if (err != ERROR_IO_PENDING) {
        return map_read_error(err);
    }

    DWORD waited = ::WaitForSingleObject(event, 100);
    if (waited == WAIT_TIMEOUT) {
        ::CancelIoEx(handle, &ov);
    }

    DWORD ov_bytes = 0;
    BOOL got = ::GetOverlappedResult(handle, &ov, &ov_bytes, TRUE);
    if (!got) {
        DWORD oerr = ::GetLastError();
        if (oerr == ERROR_OPERATION_ABORTED) {
            if (ov_bytes > 0) {
                return ReadResult{static_cast<std::size_t>(ov_bytes),
                                  false, false};
            }
            return ReadResult{0, true, false};
        }
        return map_read_error(oerr);
    }
    if (ov_bytes == 0) {
        // OVERLAPPED completion with zero bytes signals peer disconnect
        // (e.g. ERROR_BROKEN_PIPE on a pipe; hangup on a real COM port
        // on some drivers). Match the PosixSerialPort n==0 -> error=true
        // semantic: the reader loop closes the port and exits.
        return ReadResult{0, false, true};
    }
    return ReadResult{static_cast<std::size_t>(ov_bytes), false, false};
}

WriteResult Win32SerialPort::write(std::span<const std::uint8_t> bytes) {
    std::uintptr_t raw = handle_raw_.load(std::memory_order_acquire);
    if (raw == kInvalidHandle) {
        return WriteResult{0, true};
    }
    HANDLE handle = reinterpret_cast<HANDLE>(raw);
    HANDLE event = reinterpret_cast<HANDLE>(write_event_raw_);

    std::size_t total = 0;
    while (total < bytes.size()) {
        OVERLAPPED ov{};
        ::ResetEvent(event);
        ov.hEvent = event;

        DWORD chunk = static_cast<DWORD>(bytes.size() - total);
        DWORD written = 0;
        BOOL ok = ::WriteFile(handle, bytes.data() + total, chunk,
                              &written, &ov);
        if (!ok) {
            DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return WriteResult{total, true};
            }
            if (!::GetOverlappedResult(handle, &ov, &written, TRUE)) {
                return WriteResult{total, true};
            }
        }
        if (written == 0) {
            return WriteResult{total, true};
        }
        total += static_cast<std::size_t>(written);
    }
    return WriteResult{total, false};
}

}  // namespace beebium::serial

#endif  // _WIN32
