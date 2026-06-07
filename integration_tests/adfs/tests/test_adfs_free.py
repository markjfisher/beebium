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

"""ADFS integration test: *FREE display on blank discs of different sizes."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.screen import screen_contains, read_mode7_screen, dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_free.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_free.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_free_2mb(bbc_adfs):
    """*FREE on a blank 2 MB disc shows expected free space values."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("FREE") == "PASS", \
        f"*FREE failed:\n{dump_screen(bbc_adfs)}"

    rows = read_mode7_screen(bbc_adfs)
    screen_text = "\n".join(rows)
    assert "Bytes Free" in screen_text or "Bytes free" in screen_text, \
        f"'Bytes free' not found on screen:\n{dump_screen(bbc_adfs)}"


def test_adfs_free_4mb(bbc_adfs_4mb):
    """*FREE on a blank 4 MB disc shows expected free space values."""
    ok = load_and_run(bbc_adfs_4mb)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs_4mb)}"
    results = parse_results(bbc_adfs_4mb)
    assert results.get("FREE") == "PASS", \
        f"*FREE failed:\n{dump_screen(bbc_adfs_4mb)}"

    rows = read_mode7_screen(bbc_adfs_4mb)
    screen_text = "\n".join(rows)
    assert "Bytes Free" in screen_text or "Bytes free" in screen_text, \
        f"'Bytes free' not found on screen:\n{dump_screen(bbc_adfs_4mb)}"
