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

"""ADFS integration test: BASIC sequential file I/O (OPENIN/OPENOUT, PTR#, EXT#, EOF#)."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.screen import dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_fileio.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_fileio.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_sequential_write(bbc_adfs):
    """OPENOUT/PRINT#/CLOSE# writes data to a sequential file."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("WRITE") == "PASS", \
        f"Sequential write failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_sequential_read(bbc_adfs):
    """OPENIN/INPUT#/CLOSE# reads data back correctly."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("READ") == "PASS", \
        f"Sequential read failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_ext(bbc_adfs):
    """EXT# returns the file length."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("EXT") == "PASS", \
        f"EXT# failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_eof(bbc_adfs):
    """EOF# returns true at end of file."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("EOF") == "PASS", \
        f"EOF# failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_ptr(bbc_adfs):
    """PTR# resets file position for re-reading."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("PTR") == "PASS", \
        f"PTR# failed:\n{dump_screen(bbc_adfs.memory)}"
