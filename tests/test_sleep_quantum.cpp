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

// Measure sleep precision with different platform techniques.
//
// On macOS/Linux, std::this_thread::sleep_for achieves ~100us resolution.
// On Windows, the default system timer gives ~15.6ms. This test measures
// several approaches to achieve sub-millisecond sleep on Windows:
//
//   1. Baseline: std::this_thread::sleep_for (nanosleep/Sleep)
//   2. CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Win 10 1803+)
//   3. NtSetTimerResolution to 0.5ms + sleep_for
//   4. Hybrid: NtSetTimerResolution + sleep_for + busy-wait tail
//
// The test reports median and percentile statistics for each approach.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::nanoseconds;

namespace {

constexpr int WARMUP_ITERATIONS = 10;
constexpr int SAMPLE_COUNT = 500;

struct Stats {
    int64_t min_ns;
    int64_t p25_ns;
    int64_t median_ns;
    int64_t p75_ns;
    int64_t p95_ns;
    int64_t max_ns;
    double mean_ns;
};

Stats compute_stats(std::vector<int64_t>& samples) {
    std::sort(samples.begin(), samples.end());
    auto n = samples.size();
    double sum = 0;
    for (auto s : samples) sum += static_cast<double>(s);
    return {
        .min_ns = samples[0],
        .p25_ns = samples[n / 4],
        .median_ns = samples[n / 2],
        .p75_ns = samples[3 * n / 4],
        .p95_ns = samples[n * 95 / 100],
        .max_ns = samples[n - 1],
        .mean_ns = sum / static_cast<double>(n),
    };
}

void print_stats(const char* label, const Stats& s) {
    std::cout << label << ":\n"
              << "  min=" << s.min_ns / 1000 << " us"
              << "  p25=" << s.p25_ns / 1000 << " us"
              << "  median=" << s.median_ns / 1000 << " us"
              << "  p75=" << s.p75_ns / 1000 << " us"
              << "  p95=" << s.p95_ns / 1000 << " us"
              << "  max=" << s.max_ns / 1000 << " us"
              << "  mean=" << static_cast<int64_t>(s.mean_ns / 1000) << " us"
              << "\n";
}

// Measure sleep duration for a given sleep function.
// sleep_fn takes a target duration in nanoseconds.
template <typename SleepFn>
std::vector<int64_t> measure_sleep(SleepFn sleep_fn, int64_t target_ns) {
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        sleep_fn(target_ns);
    }

    std::vector<int64_t> samples;
    samples.reserve(SAMPLE_COUNT);
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        auto before = Clock::now();
        sleep_fn(target_ns);
        auto after = Clock::now();
        auto ns = std::chrono::duration_cast<Duration>(after - before).count();
        samples.push_back(ns);
    }
    return samples;
}

// --- Sleep implementations ---

void sleep_std(int64_t target_ns) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(target_ns));
}

void sleep_std_1ns(int64_t /*target_ns*/) {
    // Minimum-length sleep: measures the platform's actual quantum.
    std::this_thread::sleep_for(std::chrono::nanoseconds(1));
}

#ifdef _WIN32

// Function pointers for ntdll functions (resolved at runtime).
using NtSetTimerResolution_t = LONG(__stdcall*)(ULONG, BOOLEAN, PULONG);
using NtDelayExecution_t = LONG(__stdcall*)(BOOLEAN, PLARGE_INTEGER);

struct NtdllFunctions {
    NtSetTimerResolution_t NtSetTimerResolution = nullptr;
    NtDelayExecution_t NtDelayExecution = nullptr;
    bool loaded = false;

    void load() {
        if (loaded) return;
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) {
            NtSetTimerResolution = reinterpret_cast<NtSetTimerResolution_t>(
                GetProcAddress(ntdll, "NtSetTimerResolution"));
            NtDelayExecution = reinterpret_cast<NtDelayExecution_t>(
                GetProcAddress(ntdll, "NtDelayExecution"));
        }
        loaded = true;
    }
};

static NtdllFunctions g_ntdll;

void set_timer_resolution_max() {
    g_ntdll.load();
    if (g_ntdll.NtSetTimerResolution) {
        ULONG actual;
        g_ntdll.NtSetTimerResolution(1, TRUE, &actual);
        std::cout << "NtSetTimerResolution: requested 100ns, actual "
                  << actual * 100 << " ns (" << actual / 10 << " us)\n";
    }
}

void restore_timer_resolution() {
    g_ntdll.load();
    if (g_ntdll.NtSetTimerResolution) {
        ULONG actual;
        g_ntdll.NtSetTimerResolution(0, FALSE, &actual);
    }
}

void sleep_nt_delay(int64_t target_ns) {
    g_ntdll.load();
    if (g_ntdll.NtDelayExecution) {
        LARGE_INTEGER interval;
        // Negative = relative time, in 100ns units
        interval.QuadPart = -(static_cast<LONGLONG>(target_ns) / 100);
        if (interval.QuadPart == 0) interval.QuadPart = -1;
        g_ntdll.NtDelayExecution(FALSE, &interval);
    }
}

void sleep_waitable_timer_high_res(int64_t target_ns) {
    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET,
        TIMER_ALL_ACCESS);
    if (!timer) {
        // Fallback if HIGH_RESOLUTION not supported
        std::this_thread::sleep_for(std::chrono::nanoseconds(target_ns));
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -(static_cast<LONGLONG>(target_ns) / 100);
    if (due.QuadPart == 0) due.QuadPart = -1;
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

// Reusable timer version: avoids Create/Close overhead per sleep.
class HighResTimer {
public:
    HighResTimer() {
        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET,
            TIMER_ALL_ACCESS);
    }
    ~HighResTimer() {
        if (timer_) CloseHandle(timer_);
    }
    HighResTimer(const HighResTimer&) = delete;
    HighResTimer& operator=(const HighResTimer&) = delete;

    bool valid() const { return timer_ != nullptr; }

    void sleep(int64_t target_ns) {
        LARGE_INTEGER due;
        due.QuadPart = -(static_cast<LONGLONG>(target_ns) / 100);
        if (due.QuadPart == 0) due.QuadPart = -1;
        SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer_, INFINITE);
    }

private:
    HANDLE timer_ = nullptr;
};

void sleep_hybrid(int64_t target_ns) {
    // Sleep for most of the duration, busy-wait the last 200us.
    constexpr int64_t busywait_ns = 200'000; // 200us

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    if (target_ns > busywait_ns) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds(target_ns - busywait_ns));
    }

    // Busy-wait the remainder
    for (;;) {
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        int64_t elapsed_ns = (t1.QuadPart - t0.QuadPart) * 1'000'000'000LL / freq.QuadPart;
        if (elapsed_ns >= target_ns) break;
        // Yield if we have more than 20us left
        if (target_ns - elapsed_ns > 20'000) {
            SwitchToThread();
        }
    }
}

#endif // _WIN32

} // anonymous namespace

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("Sleep quantum: baseline std::sleep_for(1ns)", "[sleep][quantum]") {
    auto samples = measure_sleep(sleep_std_1ns, 1);
    auto stats = compute_stats(samples);
    print_stats("std::sleep_for(1ns) -- platform quantum", stats);
}

TEST_CASE("Sleep quantum: std::sleep_for(100us)", "[sleep][quantum]") {
    auto samples = measure_sleep(sleep_std, 100'000);
    auto stats = compute_stats(samples);
    print_stats("std::sleep_for(100us)", stats);
}

TEST_CASE("Sleep quantum: std::sleep_for(500us)", "[sleep][quantum]") {
    auto samples = measure_sleep(sleep_std, 500'000);
    auto stats = compute_stats(samples);
    print_stats("std::sleep_for(500us)", stats);
}

TEST_CASE("Sleep quantum: std::sleep_for(1ms)", "[sleep][quantum]") {
    auto samples = measure_sleep(sleep_std, 1'000'000);
    auto stats = compute_stats(samples);
    print_stats("std::sleep_for(1ms)", stats);
}

#ifdef _WIN32

TEST_CASE("Sleep quantum: NtSetTimerResolution + sleep_for(1ns)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_std_1ns, 1);
    auto stats = compute_stats(samples);
    print_stats("NtSetTimerResolution + sleep_for(1ns)", stats);
    restore_timer_resolution();
}

TEST_CASE("Sleep quantum: NtSetTimerResolution + sleep_for(100us)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_std, 100'000);
    auto stats = compute_stats(samples);
    print_stats("NtSetTimerResolution + sleep_for(100us)", stats);
    restore_timer_resolution();
}

TEST_CASE("Sleep quantum: NtSetTimerResolution + sleep_for(500us)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_std, 500'000);
    auto stats = compute_stats(samples);
    print_stats("NtSetTimerResolution + sleep_for(500us)", stats);
    restore_timer_resolution();
}

TEST_CASE("Sleep quantum: NtDelayExecution(100us)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_nt_delay, 100'000);
    auto stats = compute_stats(samples);
    print_stats("NtDelayExecution(100us)", stats);
    restore_timer_resolution();
}

TEST_CASE("Sleep quantum: NtDelayExecution(500us)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_nt_delay, 500'000);
    auto stats = compute_stats(samples);
    print_stats("NtDelayExecution(500us)", stats);
    restore_timer_resolution();
}

TEST_CASE("Sleep quantum: CreateWaitableTimer HIGH_RES (100us, per-call)", "[sleep][quantum][windows]") {
    auto samples = measure_sleep(sleep_waitable_timer_high_res, 100'000);
    auto stats = compute_stats(samples);
    print_stats("CreateWaitableTimer HIGH_RES (100us, per-call)", stats);
}

TEST_CASE("Sleep quantum: CreateWaitableTimer HIGH_RES (500us, per-call)", "[sleep][quantum][windows]") {
    auto samples = measure_sleep(sleep_waitable_timer_high_res, 500'000);
    auto stats = compute_stats(samples);
    print_stats("CreateWaitableTimer HIGH_RES (500us, per-call)", stats);
}

TEST_CASE("Sleep quantum: CreateWaitableTimer HIGH_RES (100us, reused)", "[sleep][quantum][windows]") {
    HighResTimer timer;
    REQUIRE(timer.valid());
    auto samples = measure_sleep([&](int64_t ns) { timer.sleep(ns); }, 100'000);
    auto stats = compute_stats(samples);
    print_stats("CreateWaitableTimer HIGH_RES (100us, reused)", stats);
}

TEST_CASE("Sleep quantum: CreateWaitableTimer HIGH_RES (500us, reused)", "[sleep][quantum][windows]") {
    HighResTimer timer;
    REQUIRE(timer.valid());
    auto samples = measure_sleep([&](int64_t ns) { timer.sleep(ns); }, 500'000);
    auto stats = compute_stats(samples);
    print_stats("CreateWaitableTimer HIGH_RES (500us, reused)", stats);
}

TEST_CASE("Sleep quantum: CreateWaitableTimer HIGH_RES (1ns, reused) -- minimum quantum", "[sleep][quantum][windows]") {
    HighResTimer timer;
    REQUIRE(timer.valid());
    auto samples = measure_sleep([&](int64_t /*ns*/) { timer.sleep(1); }, 1);
    auto stats = compute_stats(samples);
    print_stats("CreateWaitableTimer HIGH_RES (1ns, reused) -- minimum", stats);
}

TEST_CASE("Sleep quantum: hybrid sleep+busywait (100us)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_hybrid, 100'000);
    auto stats = compute_stats(samples);
    print_stats("Hybrid sleep+busywait (100us)", stats);
    restore_timer_resolution();
}

TEST_CASE("Sleep quantum: hybrid sleep+busywait (500us)", "[sleep][quantum][windows]") {
    set_timer_resolution_max();
    auto samples = measure_sleep(sleep_hybrid, 500'000);
    auto stats = compute_stats(samples);
    print_stats("Hybrid sleep+busywait (500us)", stats);
    restore_timer_resolution();
}

#endif // _WIN32
