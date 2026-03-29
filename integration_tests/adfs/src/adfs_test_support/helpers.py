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

"""Test helpers for ADFS integration tests."""

from __future__ import annotations

from beebium.client import Beebium
from beebium.screen import screen_contains, read_mode7_screen, dump_screen


def load_and_run(bbc: Beebium, emulated_seconds: float = 30.0) -> bool:
    """Switch to DFS, CHAIN the test program (which starts with *ADFS).

    Returns True if the program completed (DONE appeared on screen).
    """
    # Switch to DFS to access the floppy
    bbc.keyboard.type("*DISC\r")
    ok = bbc.run_until_or_timeout(
        lambda: screen_contains(bbc.memory, ">"),
        emulated_seconds=5.0,
    )
    assert ok, f"Failed to get prompt after *DISC:\n{dump_screen(bbc.memory)}"

    # Load and run the test program from floppy
    bbc.keyboard.type('CHAIN "TEST"\r')
    ok = bbc.run_until_or_timeout(
        lambda: screen_contains(bbc.memory, "DONE"),
        emulated_seconds=emulated_seconds,
    )
    return ok


def parse_results(bbc: Beebium) -> dict[str, str]:
    """Parse TEST:name:PASS/FAIL lines from the MODE 7 screen."""
    rows = read_mode7_screen(bbc.memory)
    results = {}
    for row in rows:
        row = row.strip()
        if row.startswith("TEST:"):
            parts = row.split(":")
            if len(parts) >= 3:
                results[parts[1]] = parts[2]
    return results
