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

#include <functional>
#include <optional>
#include <string>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
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

    inline BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
        switch (ctrl_type) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                EnterCriticalSection(&g_callback_lock);
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
    inline ShutdownCallback g_shutdown_callback;

    inline void signal_handler(int /*signal*/) {
        if (g_shutdown_callback) {
            g_shutdown_callback();
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

inline bool is_stdin_tty() {
    return isatty(STDIN_FILENO) != 0;
}

inline bool is_stdout_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

#endif  // _WIN32

// Cross-platform getenv wrapper (avoids MSVC C4996 warning)
inline std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) == 0 && value != nullptr) {
        std::string result(value);
        free(value);
        return result;
    }
    return std::nullopt;
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

}  // namespace beebium::server::platform

#endif  // BEEBIUM_SERVER_PLATFORM_HPP
