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

"""Integration tests for User Port RTC via BBC BASIC.

These tests run a BBC BASIC program that bit-bangs the SAF3019P CBUS protocol
through the User VIA, reads time registers and performs the dongle detection
sequence. The RTC is initialised to 1985-06-15T14:30 for deterministic results.
"""

from __future__ import annotations

import pytest

from beebium.screen import screen_contains, dump_screen, read_mode7_screen


def _load_and_run(bbc, emulated_seconds: float = 30.0) -> bool:
    """CHAIN the test program from disc and wait for DONE."""
    bbc.keyboard.type('CHAIN "TEST"\r')
    ok = bbc.run_until_or_timeout(
        lambda: screen_contains(bbc, "DONE"),
        emulated_seconds=emulated_seconds,
    )
    return ok


def _parse_results(bbc) -> dict[str, str]:
    """Parse TEST:name:value lines from the MODE 7 screen."""
    rows = read_mode7_screen(bbc)
    results = {}
    for row in rows:
        row = row.strip()
        if row.startswith("TEST:"):
            parts = row.split(":")
            if len(parts) >= 3:
                results[parts[1]] = parts[2]
    return results


@pytest.mark.timeout(120)
def test_rtc_dongle_detection(bbc_rtc):
    """The dongle detection sequence (write/read register 7) passes."""
    ok = _load_and_run(bbc_rtc)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_rtc)}"

    results = _parse_results(bbc_rtc)
    assert results.get("DONGLE1") == "PASS", f"Dongle phase 1 failed: {results}"
    assert results.get("DONGLE2") == "PASS", f"Dongle phase 2 failed: {results}"


@pytest.mark.timeout(120)
def test_rtc_reads_correct_time(bbc_rtc):
    """Time registers contain the values set at startup (1985-06-15 14:30)."""
    ok = _load_and_run(bbc_rtc)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_rtc)}"

    results = _parse_results(bbc_rtc)

    # The RTC was initialised to 1985-06-15T14:30.
    # Minutes may have advanced slightly during boot, so allow a small range.
    assert results.get("MONTH") == "6", f"Expected month 6, got {results.get('MONTH')}"
    assert results.get("DAY") == "15", f"Expected day 15, got {results.get('DAY')}"
    assert results.get("HOUR") == "14", f"Expected hour 14, got {results.get('HOUR')}"

    minute = int(results.get("MINUTE", "-1"))
    assert 30 <= minute <= 32, f"Expected minute ~30, got {minute}"

    # Year offset: 1985 - 1981 = 4
    assert results.get("YEAR") == "4", f"Expected year offset 4, got {results.get('YEAR')}"
