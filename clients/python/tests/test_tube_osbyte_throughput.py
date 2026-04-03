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


OSBYTE_COUNT = 16


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

    # Type a minimal program: poke machine code, print =GO, CALL, print =OK
    lines = [
        '10 FOR I%=0 TO 12:READ B%:?(&2000+I%)=B%:NEXT',
        f'20 ?&2030=256-{OSBYTE_COUNT}',
        '30 PRINT "=GO"',
        '40 CALL &2000',
        '50 PRINT "=OK"',
        '60 DATA &A9,&33,&A2,0,&20,&F4,&FF,&EE,&30,&20,&D0,&F4,&60',
        'RUN',
    ]
    for line in lines:
        bbc.keyboard.type(line + "\r")
        time.sleep(0.3)

    # Wait for =GO (program started, about to call OSBYTE loop)
    assert _wait_for(bbc, "=GO", timeout=60), \
        f"Program did not reach =GO:\n{dump_screen(bbc.memory)}"

    t0 = time.monotonic()

    # Wait for =OK (OSBYTE loop complete)
    ok = _wait_for(bbc, "=OK", timeout=120)
    t1 = time.monotonic()
    wall_seconds = t1 - t0

    print(f"\n{dump_screen(bbc.memory)}")

    if not ok:
        pytest.fail(
            f"{OSBYTE_COUNT} OSBYTE 51 calls did not complete in 120 wall-clock "
            f"seconds under normal pacing."
        )

    rate = OSBYTE_COUNT / wall_seconds
    latency = wall_seconds / OSBYTE_COUNT * 1000

    print(f"\n{'='*50}")
    print(f"  {OSBYTE_COUNT} OSBYTE 51 calls: {wall_seconds:.2f}s")
    print(f"  Throughput: {rate:.0f} OSBYTE/s")
    print(f"  Latency:    {latency:.1f} ms/call")
    print(f"  Expected:   ~1100 OSBYTE/s, ~0.9 ms/call")
    print(f"{'='*50}")

    assert rate > 500, (
        f"OSBYTE throughput {rate:.0f}/s ({latency:.1f}ms each), "
        f"expected >500/s."
    )
