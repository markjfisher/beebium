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

#ifndef BEEBIUM_SERVER_PLATFORM_HPP
#define BEEBIUM_SERVER_PLATFORM_HPP

#include <beebium/PlatformUtils.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <csignal>
#endif

namespace beebium::server::platform {

// Callback type for shutdown handlers
using ShutdownCallback = std::function<void()>;

#ifdef _WIN32

// Windows implementation using SetConsoleCtrlHandler.
// Note: Console control handlers run in a separate thread on Windows.

namespace detail {
    inline ShutdownCallback g_shutdown_callback;
    inline CRITICAL_SECTION g_callback_lock;
    inline bool g_callback_lock_initialized = false;
    inline HANDLE g_child_process = INVALID_HANDLE_VALUE;

    inline BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
        switch (ctrl_type) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                EnterCriticalSection(&g_callback_lock);
                // Terminate child subprocess before invoking the shutdown callback,
                // so it receives the signal even if this process is killed shortly after.
                if (g_child_process != INVALID_HANDLE_VALUE) {
                    TerminateProcess(g_child_process, 1);
                }
                if (g_shutdown_callback) {
                    g_shutdown_callback();
                }
                LeaveCriticalSection(&g_callback_lock);
                return TRUE;
            default:
                return FALSE;
        }
    }
}  // namespace detail

inline void install_shutdown_handler(ShutdownCallback callback) {
    if (!detail::g_callback_lock_initialized) {
        InitializeCriticalSection(&detail::g_callback_lock);
        detail::g_callback_lock_initialized = true;
    }
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_shutdown_callback = std::move(callback);
    LeaveCriticalSection(&detail::g_callback_lock);
    SetConsoleCtrlHandler(detail::console_ctrl_handler, TRUE);
}

inline void remove_shutdown_handler() {
    SetConsoleCtrlHandler(detail::console_ctrl_handler, FALSE);
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_shutdown_callback = nullptr;
    LeaveCriticalSection(&detail::g_callback_lock);
}

/// Register a child process handle for automatic termination when this
/// process receives a shutdown signal. The handle is NOT owned by this
/// module; the caller must keep it alive until unregister_child_process().
inline void register_child_process(HANDLE handle) {
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_child_process = handle;
    LeaveCriticalSection(&detail::g_callback_lock);
}

inline void unregister_child_process() {
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_child_process = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&detail::g_callback_lock);
}

/// No-op on Windows. The console control handler dispatches the callback
/// directly from its own thread (protected by a critical section).
inline void dispatch_pending_signal() {}

inline bool is_stdin_tty() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    return GetConsoleMode(h, &mode) != 0;
}

inline bool is_stdout_tty() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    return GetConsoleMode(h, &mode) != 0;
}

#else  // POSIX

namespace detail {
    inline std::atomic<bool> g_signal_received{false};
    inline std::atomic<pid_t> g_child_pid{-1};
    inline ShutdownCallback g_shutdown_callback;

    inline void signal_handler(int /*signal*/) {
        // Set the atomic flag for the main loop to pick up.
        g_signal_received.store(true, std::memory_order_relaxed);
        // Also forward the signal to the child subprocess (if any) so it
        // begins shutting down immediately, even if this process is killed
        // before reaching the normal cleanup path.
        // Both kill() and atomic load are async-signal-safe.
        pid_t child = g_child_pid.load(std::memory_order_relaxed);
        if (child > 0) {
            kill(child, SIGTERM);
        }
    }

    /// Call from the main loop to check for pending signals and dispatch
    /// the shutdown callback. Safe to call from any non-signal context.
    inline void dispatch_pending_signal() {
        if (g_signal_received.load(std::memory_order_relaxed)) {
            g_signal_received.store(false, std::memory_order_relaxed);
            if (g_shutdown_callback) {
                g_shutdown_callback();
            }
        }
    }
}  // namespace detail

inline void install_shutdown_handler(ShutdownCallback callback) {
    detail::g_shutdown_callback = std::move(callback);
    std::signal(SIGINT, detail::signal_handler);
    std::signal(SIGTERM, detail::signal_handler);
}

inline void remove_shutdown_handler() {
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    detail::g_shutdown_callback = nullptr;
}

/// Register a child process PID for automatic SIGTERM when this process
/// receives a shutdown signal. This ensures the child is signalled even
/// if the parent is killed before reaching normal cleanup code.
inline void register_child_process(pid_t pid) {
    detail::g_child_pid.store(pid, std::memory_order_relaxed);
}

inline void unregister_child_process() {
    detail::g_child_pid.store(-1, std::memory_order_relaxed);
}

/// Check for pending shutdown signal and dispatch the callback if received.
/// Call this from the main emulation loop (non-signal context).
inline void dispatch_pending_signal() {
    detail::dispatch_pending_signal();
}

inline bool is_stdin_tty() {
    return isatty(STDIN_FILENO) != 0;
}

inline bool is_stdout_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

#endif  // _WIN32

// Delegate to beebium::platform utilities in core.
inline int get_pid() { return beebium::platform::get_pid(); }
inline std::optional<std::string> get_env(const char* name) { return beebium::platform::get_env(name); }

}  // namespace beebium::server::platform

#endif  // BEEBIUM_SERVER_PLATFORM_HPP
