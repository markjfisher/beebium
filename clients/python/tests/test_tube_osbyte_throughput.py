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

Measures wall-clock time for OSBYTE calls through the Tube under normal
paced execution. No coupled stepping -- pure real-time pacing throughout,
matching interactive use.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.exceptions import ServerNotFoundError
from beebium.screen import screen_contains, dump_screen, read_mode7_screen


OSBYTE_COUNT = 32


def _wait_for(bbc, text, timeout=60):
    """Wait for text to appear at the start of a screen row."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for row in read_mode7_screen(bbc.memory):
            if row.strip().startswith(text):
                return True
        time.sleep(0.5)
    return False


@pytest.fixture(scope="function")
def bbc(beebium_roms_dirpath: Path, mos_filepath: Path, basic_filepath: Path | None):
    """Model B ROM/RAM board with Tube and ANFS. No coupled stepping."""
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    server = None
    for c in [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]:
        if c.exists():
            server = c
            break
    if server is None:
        pytest.skip("beebium-model-b-romram not found")

    anfs = beebium_roms_dirpath / "acorn-anfs_4_18.rom"
    if not anfs.exists():
        pytest.skip(f"ANFS ROM not found: {anfs}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server,
            extra_args=[
                "--sideways", f"9:rom:{anfs}",
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
def test_tube_osbyte_throughput(bbc):
    """Measure OSBYTE 51 throughput through the Tube under normal pacing."""

    # Wait for boot (normal pacing, no coupled stepping)
    assert _wait_for(bbc, ">", timeout=30), \
        f"Boot failed:\n{dump_screen(bbc.memory)}"
    assert screen_contains(bbc.memory, "TUBE"), \
        f"Tube not active:\n{dump_screen(bbc.memory)}"

    # BASIC program: poke a 1-OSBYTE machine code routine, then call it
    # N times, recording TIME before and after each call. Print results.
    #
    # Machine code at &2000 (7 bytes):
    #   LDA #51 / LDX #0 / JSR &FFF4 / RTS
    #
    # BASIC times each call individually via TIME, storing results in
    # an array. Then prints each latency in centiseconds.
    lines = [
        '10 FOR I%=0 TO 7:READ B%:?(&2000+I%)=B%:NEXT',
        f'20 N%={OSBYTE_COUNT}',
        '30 DIM R%(N%)',
        '40 PRINT "=GO"',
        '50 FOR I%=1 TO N%',
        '60 T%=TIME:CALL &2000:R%(I%)=TIME-T%',
        '70 NEXT',
        '80 FOR I%=1 TO N%:PRINT R%(I%);:NEXT',
        '90 PRINT:PRINT "=OK"',
        '100 DATA &A9,&33,&A2,0,&20,&F4,&FF,&60',
        'RUN',
    ]
    for line in lines:
        bbc.keyboard.type(line + "\r")
        time.sleep(0.3)

    # Wait for =GO
    assert _wait_for(bbc, "=GO", timeout=60), \
        f"Program did not reach =GO:\n{dump_screen(bbc.memory)}"

    t0 = time.monotonic()

    # Wait for =OK
    ok = _wait_for(bbc, "=OK", timeout=180)
    t1 = time.monotonic()
    wall_seconds = t1 - t0

    rows = read_mode7_screen(bbc.memory)
    print(f"\n{dump_screen(bbc.memory)}")

    if not ok:
        print(f"\n{dump_screen(bbc.memory)}")
        pytest.fail(
            f"{OSBYTE_COUNT} OSBYTE 51 calls did not complete in 180 wall-clock "
            f"seconds under normal pacing."
        )

    # Parse per-call centisecond values from the output.
    # Values are printed space-separated and may wrap across multiple rows.
    # Due to scrolling, some may appear ABOVE =GO on screen. Collect all
    # integers from rows that contain only numbers (not program listings).
    latencies_cs = []
    for row in rows:
        s = row.strip()
        if not s or s.startswith(">") or s.startswith("="):
            continue
        # Row contains only numbers (BASIC PRINT output)
        for token in s.split():
            try:
                latencies_cs.append(int(token))
            except ValueError:
                pass

    # Convert centiseconds to milliseconds
    latencies_ms = [cs * 10.0 for cs in latencies_cs]

    wall_rate = OSBYTE_COUNT / wall_seconds
    wall_latency = wall_seconds / OSBYTE_COUNT * 1000

    print(f"\n{'='*50}")
    print(f"  Tube OSBYTE 51 latency distribution")
    print(f"{'='*50}")
    print(f"  Calls:     {OSBYTE_COUNT}")
    print(f"  Wall time: {wall_seconds:.2f}s")
    print(f"  Wall avg:  {wall_latency:.1f} ms/call")
    print(f"  Wall rate: {wall_rate:.0f} OSBYTE/s")

    if latencies_ms:
        avg = sum(latencies_ms) / len(latencies_ms)
        lo = min(latencies_ms)
        hi = max(latencies_ms)
        print(f"\n  BASIC TIME per call (10ms resolution):")
        print(f"    min:  {lo:.0f} ms")
        print(f"    mean: {avg:.1f} ms")
        print(f"    max:  {hi:.0f} ms")
        print(f"    raw (cs): {' '.join(str(cs) for cs in latencies_cs)}")

    print(f"\n  Expected: ~0.9 ms/call, ~1100 OSBYTE/s")
    print(f"{'='*50}")

    assert wall_rate > 500, (
        f"OSBYTE throughput {wall_rate:.0f}/s ({wall_latency:.1f}ms each), "
        f"expected >500/s."
    )
