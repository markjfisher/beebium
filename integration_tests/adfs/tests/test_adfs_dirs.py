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

"""ADFS integration test: directory operations (*CDIR, *DIR, *BACK, *TITLE)."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.screen import screen_contains, dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_dirs.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_dirs.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_cdir(bbc_adfs):
    """*CDIR creates a directory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("CDIR") == "PASS", \
        f"*CDIR failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_dir_into(bbc_adfs):
    """*DIR changes into a subdirectory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("DIR-INTO") == "PASS", \
        f"*DIR into subdirectory failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_dir_root(bbc_adfs):
    """*DIR $ returns to root directory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("DIR-ROOT") == "PASS", \
        f"*DIR $ failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_back(bbc_adfs):
    """*BACK swaps between previous and current directories."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("BACK") == "PASS", \
        f"*BACK failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_title(bbc_adfs):
    """*TITLE sets directory title visible in *CAT output."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("TITLE") == "PASS", \
        f"*TITLE failed:\n{dump_screen(bbc_adfs)}"
    assert screen_contains(bbc_adfs, "My Disc"), \
        f"Title 'My Disc' not found on screen:\n{dump_screen(bbc_adfs)}"
