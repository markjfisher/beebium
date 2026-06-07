// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// test_keyboard_pacing.cpp
//
// Measurement harness for keyboard typing reliability (issue #49).
//
// These tests do not assert a fixed "correct" answer; they MEASURE the timing
// behaviour of the real MOS 1.20 keyboard scan so we can choose hold/gap
// durations from evidence rather than guesswork. They print result tables to
// stdout. Tagged [.] so they are hidden from the default run; invoke directly:
//
//   ./tests/test_keyboard_pacing "[pacing]"
//
// Experiment 0 (white-box): hold one key and record every MOS probe of it via
//   the SystemViaPeripheral probe observer -> the real scan interval and the
//   worst-case first-sample latency.
// Experiment A (black-box): sweep HOLD with a generous gap, across scan-phase
//   offsets, typing distinct characters -> minimum reliable hold (drops).
// Experiment B (black-box): sweep GAP with a generous hold, typing REPEATED
//   characters -> minimum reliable gap (merges, the OPALSS case).
// Experiment C (black-box): sweep HOLD upward -> the hold at which MOS
//   auto-repeat starts doubling a single press.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "test_keyboard_helpers.hpp"

using namespace beebium;
using namespace beebium::test;

#ifdef BEEBIUM_ROM_DIR

namespace {

// 2 MHz: 2000 cycles per millisecond.
constexpr int kCyclesPerMs = 2000;
constexpr int ms(int milliseconds) { return milliseconds * kCyclesPerMs; }

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// A booted Model B at the MODE 7 BASIC prompt, with a manually advanced cycle
// clock and helpers that drive the keyboard matrix with independent hold/gap.
struct Rig {
    ModelB machine;
    HeapFrameAllocator allocator;
    FrameBuffer fb{&allocator, 640, 512};
    FrameRenderer renderer{&fb};
    uint64_t cycle = 0;

    bool boot() {
        const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
        if (!std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom") ||
            !std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom")) {
            return false;
        }
        auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
        auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().enable_video_output();
        machine.reset();

        bool booted = false;
        for (uint64_t i = 0; i < 4'000'000 && !booted; ++i) {
            step();
            if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 7) booted = true;
        }
        if (!booted) return false;
        run(ms(150));  // let the cursor settle
        return true;
    }

    void step() {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        ++cycle;
    }

    void run(int cycles) {
        for (int i = 0; i < cycles; ++i) step();
    }

    SystemViaPeripheral& via() { return machine.state().memory.system_via_peripheral; }

    // Press (row,col) for hold cycles, release for gap cycles.
    void tap(uint8_t row, uint8_t col, int hold, int gap) {
        via().key_down(row, col);
        run(hold);
        via().key_up(row, col);
        run(gap);
    }

    // Type an ASCII string (letters/digits only here) with explicit hold/gap.
    void type(const std::string& text, int hold, int gap) {
        for (char c : text) {
            uint8_t kc = ascii_to_keycode(static_cast<uint8_t>(c));
            if (kc == 0xFF) continue;
            tap((kc >> 4) & 0x0F, kc & 0x0F, hold, gap);
        }
    }

    // Text after the last '>' on the bottom-most row that has one (the active
    // input line), trailing spaces stripped. BASIC may indent the prompt, so
    // this is column-agnostic. Returns "<no prompt>" if no '>' is on screen.
    std::string read_input_line() {
        for (int row = 24; row >= 0; --row) {
            std::string line = read_mode7_screen(machine, row, 0, 40);
            size_t pos = line.rfind('>');
            if (pos != std::string::npos) {
                std::string s = line.substr(pos + 1);
                while (!s.empty() && s.back() == ' ') s.pop_back();
                return s;
            }
        }
        return "<no prompt>";
    }

    // Abandon the current input line and flush the buffer with ESCAPE. Unlike
    // RETURN this never executes typed text, so a garbled trial cannot corrupt
    // machine state or scroll unpredictably; BASIC just prints "Escape" and a
    // fresh prompt. Generous, reliable timing.
    void reset_line() {
        tap(0x7, 0x0, ms(60), ms(60));  // ESCAPE (key &70)
        run(ms(150));
    }

    // Text strictly between the first `open` and the next `close` on the
    // bottom-most qualifying row, ignoring rows containing `ignore` (used to
    // skip the INPUT echo line, which begins with '?'). Returns "" if none.
    std::string read_between(char open, char close, char ignore) {
        for (int row = 24; row >= 0; --row) {
            std::string line = read_mode7_screen(machine, row, 0, 40);
            if (line.find(ignore) != std::string::npos) continue;
            size_t a = line.find(open);
            if (a == std::string::npos) continue;
            size_t b = line.find(close, a + 1);
            if (b == std::string::npos) continue;
            return line.substr(a + 1, b - a - 1);
        }
        return "";
    }

    void dump_screen() {
        std::cerr << "  --- screen ---\n";
        for (int row = 0; row < 25; ++row) {
            std::cerr << "  |" << read_mode7_screen(machine, row, 0, 40) << "|\n";
        }
    }
};

// Result of typing `expected` once at a given phase offset.
enum class Outcome { Match, Mismatch, NoPrompt };
struct Trial { Outcome outcome; std::string got; };

Trial type_and_check(Rig& rig, const std::string& expected,
                     int hold, int gap, int phase) {
    rig.reset_line();               // clean, buffer-flushed prompt
    rig.run(phase);                 // shift alignment vs the MOS scan
    if (rig.read_input_line() == "<no prompt>") {
        std::cerr << "  [no prompt before typing \"" << expected << "\"]\n";
        rig.dump_screen();
        return {Outcome::NoPrompt, "<no prompt>"};
    }
    rig.type(expected, hold, gap);
    std::string got = rig.read_input_line();
    return {got == expected ? Outcome::Match : Outcome::Mismatch, got};
}

// Phase offsets spread across one ~50 ms scan window.
const std::vector<int> kPhases = {0, ms(8), ms(17), ms(25), ms(33), ms(42)};

// Run `expected` at every phase offset; reliable iff every phase matches.
bool reliable_at(Rig& rig, const std::string& expected, int hold, int gap,
                 std::string& first_failure) {
    bool all = true;
    for (int phase : kPhases) {
        Trial t = type_and_check(rig, expected, hold, gap, phase);
        if (t.outcome != Outcome::Match && all) { first_failure = t.got; all = false; }
    }
    return all;
}

} // namespace

// ============================================================================
// Experiment 0: measure the MOS keyboard scan interval (white-box observation)
// ============================================================================
TEST_CASE("Pacing 0: MOS keyboard scan interval", "[.][pacing][keyboard]") {
    Rig rig;
    REQUIRE(rig.boot());

    const uint8_t kRow = 4, kCol = 1;  // 'A'
    std::vector<uint64_t> probe_cycles;
    rig.via().set_keyboard_probe_observer(
        [&](uint8_t r, uint8_t c, bool /*pressed*/) {
            if (r == kRow && c == kCol) probe_cycles.push_back(rig.cycle);
        });

    std::cout << "\n=== Experiment 0: MOS scan interval (key 'A') ===\n";

    // Inter-probe interval while the key is held steadily.
    probe_cycles.clear();
    rig.via().key_down(kRow, kCol);
    rig.run(ms(250));
    rig.via().key_up(kRow, kCol);

    uint64_t min_d = UINT64_MAX, max_d = 0, sum_d = 0, n = 0;
    for (size_t i = 1; i < probe_cycles.size(); ++i) {
        uint64_t d = probe_cycles[i] - probe_cycles[i - 1];
        // Ignore sub-scan re-probes within one pass (< 1 ms): we want the
        // period between successive scan passes.
        if (d < (uint64_t)ms(1)) continue;
        min_d = std::min(min_d, d);
        max_d = std::max(max_d, d);
        sum_d += d; ++n;
    }
    std::cout << "100 Hz scan cadence over " << probe_cycles.size()
              << " port-A probes (inter-pass gaps >= 1 ms, n=" << n << "):\n";
    if (n > 0) {
        std::cout << "  min=" << (min_d / (double)kCyclesPerMs)
                  << " ms  mean=" << (sum_d / (double)n / kCyclesPerMs)
                  << " ms  max=" << (max_d / (double)kCyclesPerMs) << " ms\n";
        std::cout << "  (a key must survive debounce across consecutive scans;\n"
                     "   see Experiment A for the registration threshold)\n";
    }

    rig.via().set_keyboard_probe_observer(nullptr);
    rig.reset_line();
    CHECK(n > 0);  // we observed a periodic scan
}

// ============================================================================
// Experiment A: minimum reliable HOLD (drops), generous gap
// ============================================================================
TEST_CASE("Pacing A: minimum reliable hold", "[.][pacing][keyboard]") {
    Rig rig;
    REQUIRE(rig.boot());

    const int gap = ms(60);
    const std::string text = "ABCDE";
    std::cout << "\n=== Experiment A: hold sweep (gap=" << (gap / kCyclesPerMs)
              << " ms, text=\"" << text << "\") ===\n";

    int min_reliable = -1;
    for (int hold_ms : {5, 10, 15, 20, 25, 30, 40, 50, 60}) {
        std::string fail;
        bool ok = reliable_at(rig, text, ms(hold_ms), gap, fail);
        std::cout << "  hold " << hold_ms << " ms : "
                  << (ok ? "reliable" : "FAIL (got \"" + fail + "\")") << "\n";
        if (ok && min_reliable < 0) min_reliable = hold_ms;
    }
    std::cout << "Minimum reliable hold: "
              << (min_reliable < 0 ? -1 : min_reliable) << " ms\n";
    CHECK(min_reliable > 0);
}

// ============================================================================
// Experiment B: minimum reliable GAP (repeated-key merges), generous hold
// ============================================================================
TEST_CASE("Pacing B: minimum reliable gap for repeats", "[.][pacing][keyboard]") {
    Rig rig;
    REQUIRE(rig.boot());

    const int hold = ms(60);
    const std::string text = "AABBCC";  // adjacent identical keys
    std::cout << "\n=== Experiment B: gap sweep (hold=" << (hold / kCyclesPerMs)
              << " ms, text=\"" << text << "\") ===\n";

    int min_reliable = -1;
    for (int gap_ms : {5, 10, 15, 20, 25, 30, 40, 50, 60}) {
        std::string fail;
        bool ok = reliable_at(rig, text, hold, ms(gap_ms), fail);
        std::cout << "  gap " << gap_ms << " ms : "
                  << (ok ? "reliable" : "FAIL (got \"" + fail + "\")") << "\n";
        if (ok && min_reliable < 0) min_reliable = gap_ms;
    }
    std::cout << "Minimum reliable gap (repeats): "
              << (min_reliable < 0 ? -1 : min_reliable) << " ms\n";
    CHECK(min_reliable > 0);
}

// ============================================================================
// Experiment C: HOLD at which auto-repeat starts doubling a single press
// ============================================================================
TEST_CASE("Pacing C: auto-repeat doubling threshold", "[.][pacing][keyboard]") {
    Rig rig;
    REQUIRE(rig.boot());

    const int gap = ms(60);
    std::cout << "\n=== Experiment C: single-key doubling onset (gap="
              << (gap / kCyclesPerMs) << " ms) ===\n";

    int first_double = -1;
    for (int hold_ms : {40, 200, 400, 600, 800, 1000, 1200, 1500, 2000}) {
        // Type a single 'A'; a clean single press reads back exactly "A".
        // Doubling == a Mismatch whose text is "AA", "AAA", ... (only 'A's).
        bool doubled = false;
        std::string sample;
        for (int phase : kPhases) {
            Trial t = type_and_check(rig, "A", ms(hold_ms), gap, phase);
            bool all_a = !t.got.empty() &&
                         t.got.find_first_not_of('A') == std::string::npos;
            if (t.outcome == Outcome::Mismatch && all_a && t.got.size() > 1) {
                doubled = true; sample = t.got; break;
            }
        }
        std::cout << "  hold " << hold_ms << " ms : "
                  << (doubled ? "DOUBLES (got \"" + sample + "\")" : "single") << "\n";
        if (doubled && first_double < 0) first_double = hold_ms;
    }
    std::cout << "Auto-repeat doubling begins at hold ~"
              << (first_double < 0 ? -1 : first_double) << " ms\n";
    // Informational: no hard assertion on where auto-repeat kicks in.
    SUCCEED();
}

// ============================================================================
// Experiment D: minimum reliable HOLD while a BASIC program is busy printing
// ============================================================================
//
// The idle-prompt floors (Experiments A/B) assume nothing else is running.
// Sphinx Adventure is BASIC: it prints a room (busy) and then INPUTs a command.
// This reproduces that shape: a busy FOR..PRINT loop, into which we inject the
// command via type-ahead, after which INPUT consumes the buffered keystrokes.
// We read back what the program actually received (A$, wrapped in '/').
TEST_CASE("Pacing D: minimum reliable hold while program busy", "[.][pacing][busy][keyboard]") {
    Rig rig;
    REQUIRE(rig.boot());

    // Enter the program once, at a generous reliable pace (handles the shifted
    // '$' via the shared SHIFT-aware helper). Line 10 is the busy printing;
    // line 20 consumes type-ahead; line 30 echoes it delimited by '/'.
    const int setup_speed = 120000;  // 60 ms hold / 60 ms gap
    rig.reset_line();
    type_string_with_shift(rig.machine, rig.renderer,
                           "10 FORI=1TO300:PRINTI;:NEXT\r", setup_speed);
    type_string_with_shift(rig.machine, rig.renderer, "20 INPUTA$\r", setup_speed);
    type_string_with_shift(rig.machine, rig.renderer, "30 PRINTA$\r", setup_speed);

    const int gap = ms(60);
    const std::string payload = "/ABCDE/";  // slashes are unshifted delimiters
    const std::string expected = "ABCDE";
    std::cout << "\n=== Experiment D: hold sweep while busy printing (gap="
              << (gap / kCyclesPerMs) << " ms, payload=\"" << payload << "\") ===\n";

    auto run_busy_trial = [&](int hold, int phase) -> std::string {
        rig.reset_line();  // break any prior run / INPUT, back to '>'
        type_string_with_shift(rig.machine, rig.renderer, "CLS\rRUN\r", setup_speed);
        rig.run(ms(120));            // let the FOR loop get going
        rig.run(phase);              // vary scan-phase alignment
        rig.type(payload + "\r", hold, gap);  // inject during busy printing
        // Wait for the loop to finish, INPUT to consume, and line 30 to echo.
        for (int waited = 0; waited < ms(5000); waited += ms(20)) {
            rig.run(ms(20));
            std::string got = rig.read_between('/', '/', '?');
            if (!got.empty()) return got;
        }
        return "<timeout>";  // a dropped char/RETURN leaves INPUT hung
    };

    int min_reliable = -1;
    for (int hold_ms : {15, 20, 25, 30, 40, 60}) {
        bool ok = true;
        std::string fail;
        for (int phase : {0, ms(15), ms(31)}) {
            std::string got = run_busy_trial(ms(hold_ms), phase);
            if (got != expected && ok) { ok = false; fail = got; }
        }
        std::cout << "  hold " << hold_ms << " ms : "
                  << (ok ? "reliable" : "FAIL (got \"" + fail + "\")") << "\n";
        if (ok && min_reliable < 0) min_reliable = hold_ms;
    }
    std::cout << "Minimum reliable hold while busy: "
              << (min_reliable < 0 ? -1 : min_reliable) << " ms\n";
    CHECK(min_reliable > 0);
}

#endif // BEEBIUM_ROM_DIR
