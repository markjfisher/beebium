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
    # Switch to DFS, then CHAIN the test program from floppy.
    # CHAIN does LOAD+RUN atomically. The program's first line should
    # be *ADFS to switch the filing system back to ADFS.
    #
    # We wait for the command echo and subsequent prompt to avoid
    # typing the next command before the previous one has finished.
    bbc.keyboard.type("*DISC\r")
    ok = bbc.run_until_or_timeout(
        lambda: _has_prompt_after(bbc, "*DISC"),
        emulated_seconds=5.0,
    )
    assert ok, f"*DISC failed:\n{dump_screen(bbc)}"

    bbc.keyboard.type('CHAIN "TEST"\r')
    ok = bbc.run_until_or_timeout(
        lambda: screen_contains(bbc, "DONE"),
        emulated_seconds=emulated_seconds,
    )
    return ok


def _has_prompt_after(bbc: Beebium, command: str) -> bool:
    """Check that a '>' prompt appears on a line AFTER the given command."""
    rows = read_mode7_screen(bbc)
    found_command = False
    for row in rows:
        stripped = row.strip()
        if command in stripped:
            found_command = True
        elif found_command and stripped == ">":
            return True
    return False


def parse_results(bbc: Beebium) -> dict[str, str]:
    """Parse TEST:name:PASS/FAIL lines from the MODE 7 screen."""
    rows = read_mode7_screen(bbc)
    results = {}
    for row in rows:
        row = row.strip()
        if row.startswith("TEST:"):
            parts = row.split(":")
            if len(parts) >= 3:
                results[parts[1]] = parts[2]
    return results
