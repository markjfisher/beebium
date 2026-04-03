# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""Tube OSBYTE throughput test.

Uses BASIC to poke a tight machine code OSBYTE loop into parasite RAM,
execute it, and print "DONE" on completion. The test measures wall-clock
time from CALL to DONE.

Uses OSBYTE 150 (read SHEILA byte, X=0) which MUST traverse the Tube
to the host -- the parasite cannot handle it locally.

Expected throughput on real hardware: ~1100 OSBYTE/s (~0.9ms each).
Observed in Beebium with independent 200 Hz pacing: ~67 OSBYTE/s (~15ms each).
"""

from __future__ import annotations

import time

import pytest

from beebium.screen import screen_contains, dump_screen

from tube_test_helpers import TUBE_CYCLES_PER_KEY, run_until_or_timeout


# BASIC program with inline assembler.
# Assembles a tight OSBYTE 150 loop, CALLs it, prints "DONE <centiseconds>".
# OSBYTE 150 (read SHEILA byte X=0) must traverse the Tube to the host.
BASIC_PROGRAM = (
    '10 osbyte=&FFF4\r'
    '20 DIM code% 32\r'
    '30 count=code%+24\r'
    '40 FOR pass%=0 TO 2 STEP 2\r'
    '50 P%=code%\r'
    '60 [OPT pass%\r'
    '70 .loop\r'
    '80 LDA #150\r'
    '90 LDX #0\r'
    '100 JSR osbyte\r'
    '110 INC count\r'
    '120 BNE loop\r'
    '130 RTS\r'
    '140 ]\r'
    '150 NEXT\r'
    '160 ?count=0\r'
    '170 T%=TIME\r'
    '180 CALL code%\r'
    '190 T%=TIME-T%\r'
    '200 PRINT "DONE ";T%\r'
)

OSBYTE_COUNT = 256


@pytest.mark.timeout(120)
def test_tube_osbyte_throughput(bbc_tube):
    """Measure OSBYTE throughput through the Tube.

    Pokes a loop that calls OSBYTE 150 (read SHEILA) 256 times on the
    parasite, measures elapsed centiseconds via BASIC TIME, and reports
    the throughput.
    """
    bbc = bbc_tube

    # Boot to Tube BASIC prompt
    ok = run_until_or_timeout(
        bbc,
        lambda: screen_contains(bbc.memory, ">"),
        emulated_seconds=15.0,
    )
    assert ok, "Failed to boot to BASIC prompt with Tube"

    # Type the BASIC program
    bbc.keyboard.type(BASIC_PROGRAM, cycles_per_key=TUBE_CYCLES_PER_KEY)

    # Run enough emulated time for BASIC to accept the program
    run_until_or_timeout(
        bbc,
        lambda: screen_contains(bbc.memory, ">"),
        emulated_seconds=10.0,
    )

    # Type RUN and switch to real-time pacing for the measurement
    bbc.keyboard.type("RUN\r", cycles_per_key=TUBE_CYCLES_PER_KEY)

    # Resume with real-time pacing
    bbc.debugger.ensure_running()
    # Parasite is automatically running (it wasn't stopped independently)

    t0 = time.monotonic()

    # Wait for "DONE" on screen
    while True:
        time.sleep(0.5)
        from beebium.screen import read_mode7_screen
        rows = read_mode7_screen(bbc.memory)
        screen_text = "\n".join(rows)
        if "DONE" in screen_text:
            break
        elapsed = time.monotonic() - t0
        if elapsed > 90:
            print(f"\nScreen:\n{dump_screen(bbc.memory)}")
            pytest.fail("OSBYTE loop did not complete within 90s")

    t1 = time.monotonic()
    wall_seconds = t1 - t0

    # Parse the BASIC TIME value from the screen
    # Screen should show: "DONE <centiseconds>"
    basic_cs = None
    for row in rows:
        row = row.strip()
        if row.startswith("DONE"):
            parts = row.split()
            if len(parts) >= 2:
                try:
                    basic_cs = int(parts[1])
                except ValueError:
                    pass

    osbyte_per_sec_wall = OSBYTE_COUNT / wall_seconds
    ms_per_osbyte_wall = (wall_seconds / OSBYTE_COUNT) * 1000

    print(f"\n{'='*60}")
    print(f"Tube OSBYTE throughput results")
    print(f"{'='*60}")
    print(f"  OSBYTE calls:          {OSBYTE_COUNT}")
    print(f"  Wall-clock time:       {wall_seconds:.2f}s")
    print(f"  Throughput (wall):     {osbyte_per_sec_wall:.0f} OSBYTE/s")
    print(f"  Latency (wall):        {ms_per_osbyte_wall:.1f} ms/OSBYTE")
    if basic_cs is not None:
        emulated_secs = basic_cs / 100.0
        osbyte_per_sec_emu = OSBYTE_COUNT / emulated_secs
        ms_per_osbyte_emu = (emulated_secs / OSBYTE_COUNT) * 1000
        print(f"  BASIC TIME (cs):       {basic_cs}")
        print(f"  Emulated time:         {emulated_secs:.2f}s")
        print(f"  Throughput (emulated): {osbyte_per_sec_emu:.0f} OSBYTE/s")
        print(f"  Latency (emulated):    {ms_per_osbyte_emu:.1f} ms/OSBYTE")
    print(f"  Expected (real HW):    ~1100 OSBYTE/s, ~0.9 ms/OSBYTE")
    print(f"{'='*60}")

    # The throughput should be at least 500 OSBYTE/s.
    # On real hardware it's ~1100/s. With the tick-boundary bug it's ~67/s.
    assert osbyte_per_sec_wall > 500, (
        f"OSBYTE throughput is {osbyte_per_sec_wall:.0f}/s "
        f"({ms_per_osbyte_wall:.1f} ms each), expected >500/s. "
        f"Tick-boundary latency between host and parasite pacing "
        f"is the cause."
    )
