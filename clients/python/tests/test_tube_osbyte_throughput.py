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

Measures wall-clock time for 256 OSBYTE 51 (Econet poll) calls through
the Tube with ANFS active. Uses a minimal BASIC program that pokes a
13-byte machine code loop, prints START, CALLs it, prints DONE.

Expected throughput on real hardware: ~1100 OSBYTE/s (~0.9ms each).
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.exceptions import ServerNotFoundError
from beebium.screen import screen_contains, dump_screen, read_mode7_screen

from tube_test_helpers import TUBE_CYCLES_PER_KEY, run_until_or_timeout


# Minimal BASIC program -- no assembler, no disc, just DATA/POKE/CALL.
# Machine code (13 bytes at &2000):
#   LDA #51 / LDX #0 / JSR &FFF4 / INC &2030 / BNE loop / RTS
# Counter at &2030 wraps from 0 -> 255 -> 0, giving 256 iterations.
BASIC_PROGRAM = (
    '10 FOR I%=0 TO 12:READ B%:?(&2000+I%)=B%:NEXT\r'
    '20 ?&2030=0\r'
    '30 PRINT "START"\r'
    '40 CALL &2000\r'
    '50 PRINT "DONE"\r'
    '60 DATA &A9,&33,&A2,0,&20,&F4,&FF,&EE,&30,&20,&D0,&F4,&60\r'
)

OSBYTE_COUNT = 256


@pytest.fixture(scope="function")
def bbc_tube_econet(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_roms_dirpath: Path,
) -> Beebium:
    """Model B ROM/RAM board with Tube and Econet/ANFS."""
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    romram_server = None
    for candidate in [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]:
        if candidate.exists():
            romram_server = candidate
            break
    if romram_server is None:
        pytest.skip("beebium-model-b-romram not found")

    anfs_rom = beebium_roms_dirpath / "acorn-anfs_4_18.rom"
    if not anfs_rom.exists():
        pytest.skip(f"ANFS ROM not found: {anfs_rom}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=romram_server,
            extra_args=[
                "--sideways", f"9:rom:{anfs_rom}",
                "--tube", "65C02-3MHz",
                "--station", "254",
                "--aun-port", "0",
            ],
            startup_timeout=20.0,
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.timeout(300)
def test_tube_osbyte_throughput(bbc_tube_econet):
    """Measure OSBYTE 51 throughput through the Tube with ANFS active.

    Wall-clock time between START and DONE markers gives the real-world
    throughput for 256 OSBYTE 51 calls through the Tube.
    """
    bbc = bbc_tube_econet

    # Boot to Tube BASIC prompt
    ok = run_until_or_timeout(
        bbc,
        lambda: screen_contains(bbc.memory, ">"),
        emulated_seconds=15.0,
    )
    assert ok, "Failed to boot to BASIC prompt with Tube"

    # Type the short BASIC program
    bbc.keyboard.type(BASIC_PROGRAM, cycles_per_key=TUBE_CYCLES_PER_KEY)

    # Wait for all lines to be entered
    run_until_or_timeout(
        bbc,
        lambda: screen_contains(bbc.memory, ">60"),
        emulated_seconds=60.0,
    )

    # Type RUN
    bbc.keyboard.type("RUN\r", cycles_per_key=TUBE_CYCLES_PER_KEY)

    # Resume with real-time pacing
    bbc.debugger.ensure_running()

    def _screen_has(text):
        for row in read_mode7_screen(bbc.memory):
            if row.strip().startswith(text):
                return True
        return False

    # Wait for START
    deadline = time.monotonic() + 120
    while not _screen_has("START"):
        time.sleep(0.2)
        if time.monotonic() > deadline:
            print(f"\nScreen:\n{dump_screen(bbc.memory)}")
            pytest.fail("Program did not reach START within 120s")

    t0 = time.monotonic()

    # Wait for DONE
    while not _screen_has("DONE"):
        time.sleep(0.2)
        if time.monotonic() - t0 > 120:
            print(f"\nScreen:\n{dump_screen(bbc.memory)}")
            pytest.fail("OSBYTE loop did not complete within 120s")

    t1 = time.monotonic()
    wall_seconds = t1 - t0

    wall_rate = OSBYTE_COUNT / wall_seconds
    wall_latency = wall_seconds / OSBYTE_COUNT * 1000

    print(f"\n{'='*60}")
    print(f"Tube OSBYTE 51 throughput (with ANFS)")
    print(f"{'='*60}")
    print(f"  OSBYTE calls:          {OSBYTE_COUNT}")
    print(f"  Wall-clock time:       {wall_seconds:.2f}s")
    print(f"  Throughput:            {wall_rate:.0f} OSBYTE/s")
    print(f"  Latency:               {wall_latency:.1f} ms/OSBYTE")
    print(f"  Expected (real HW):    ~1100 OSBYTE/s, ~0.9 ms/OSBYTE")
    print(f"{'='*60}")

    assert wall_rate > 500, (
        f"OSBYTE throughput is {wall_rate:.0f}/s "
        f"({wall_latency:.1f} ms each), expected >500/s."
    )
