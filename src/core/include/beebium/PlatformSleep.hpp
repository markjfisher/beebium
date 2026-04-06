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

/// Platform-optimised high-resolution sleep.
///
/// On macOS and Linux, std::this_thread::sleep_for already achieves
/// ~100us resolution, so we use it directly.
///
/// On Windows, the default system timer gives ~15.6ms resolution.
/// We combine two techniques for the shortest reliable sleep:
///
/// 1. NtSetTimerResolution -- reduces the system timer period from
///    15.6ms to ~496us. System-wide effect, standard practice for
///    games and emulators.
///
/// 2. CreateWaitableTimerExW with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
///    (Win 10 1803+) -- a reused timer handle that respects the
///    reduced timer resolution.
///
/// Together these achieve a ~496us minimum sleep quantum on Windows.
///
/// Usage:
///   PlatformSleep sleeper;
///   sleeper.sleep(std::chrono::microseconds(100));
///
/// The object is move-only (owns a Windows HANDLE on that platform).
/// Construct once, reuse for the lifetime of the timer thread.
/// The timer resolution is raised on construction and restored on
/// destruction.

#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace beebium {

class PlatformSleep {
public:
    using Duration = std::chrono::nanoseconds;

#ifdef _WIN32

    PlatformSleep() {
        // Raise system timer resolution for shorter sleeps.
        // NtSetTimerResolution is undocumented but widely used by games
        // and emulators. The resolution is process-wide; the system uses
        // the highest resolution requested by any process.
        using NtSetTimerResolution_t = LONG(__stdcall*)(ULONG, BOOLEAN, PULONG);
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) {
            auto NtSetTimerResolution = reinterpret_cast<NtSetTimerResolution_t>(
                GetProcAddress(ntdll, "NtSetTimerResolution"));
            if (NtSetTimerResolution) {
                ULONG actual;
                // Request maximum resolution (1 = 100ns units).
                // Actual resolution depends on hardware, typically ~496us.
                NtSetTimerResolution(1, TRUE, &actual);
                timer_resolution_set_ = true;
            }
        }

        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET,
            TIMER_ALL_ACCESS);
        // Falls back to sleep_for if the API is unavailable (pre-1803).
    }

    ~PlatformSleep() {
        if (timer_) CloseHandle(timer_);
        // Restore default timer resolution.
        if (timer_resolution_set_) {
            using NtSetTimerResolution_t = LONG(__stdcall*)(ULONG, BOOLEAN, PULONG);
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (ntdll) {
                auto NtSetTimerResolution = reinterpret_cast<NtSetTimerResolution_t>(
                    GetProcAddress(ntdll, "NtSetTimerResolution"));
                if (NtSetTimerResolution) {
                    ULONG actual;
                    NtSetTimerResolution(0, FALSE, &actual);
                }
            }
        }
    }

    PlatformSleep(PlatformSleep&& other) noexcept
        : timer_(other.timer_)
        , timer_resolution_set_(other.timer_resolution_set_) {
        other.timer_ = nullptr;
        other.timer_resolution_set_ = false;
    }

    PlatformSleep& operator=(PlatformSleep&& other) noexcept {
        if (this != &other) {
            if (timer_) CloseHandle(timer_);
            timer_ = other.timer_;
            timer_resolution_set_ = other.timer_resolution_set_;
            other.timer_ = nullptr;
            other.timer_resolution_set_ = false;
        }
        return *this;
    }

    void sleep(Duration duration) const {
        if (timer_) {
            LARGE_INTEGER due;
            // Negative = relative time in 100ns units
            due.QuadPart = -(duration.count() / 100);
            if (due.QuadPart == 0) due.QuadPart = -1;
            SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer_, INFINITE);
        } else {
            // Fallback for pre-1803 Windows
            std::this_thread::sleep_for(duration);
        }
    }

    /// True if the high-resolution timer is available.
    bool high_resolution() const { return timer_ != nullptr; }

#else // macOS, Linux

    PlatformSleep() = default;
    ~PlatformSleep() = default;
    PlatformSleep(PlatformSleep&&) noexcept = default;
    PlatformSleep& operator=(PlatformSleep&&) noexcept = default;

    void sleep(Duration duration) const {
        std::this_thread::sleep_for(duration);
    }

    bool high_resolution() const { return true; }

#endif

    PlatformSleep(const PlatformSleep&) = delete;
    PlatformSleep& operator=(const PlatformSleep&) = delete;

#ifdef _WIN32
private:
    HANDLE timer_ = nullptr;
    bool timer_resolution_set_ = false;
#endif
};

} // namespace beebium
