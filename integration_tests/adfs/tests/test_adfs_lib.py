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

"""ADFS integration test: library directory (*LIB, *LCAT, *LEX)."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client.screen import dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_lib.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_lib.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_cdir_lib(bbc_adfs):
    """*CDIR creates a library directory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("CDIR-LIB") == "PASS", \
        f"*CDIR for library failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_lib_set(bbc_adfs):
    """*LIB sets the library directory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("LIB-SET") == "PASS", \
        f"*LIB failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_lcat(bbc_adfs):
    """*LCAT catalogues the library directory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("LCAT") == "PASS", \
        f"*LCAT failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_lex(bbc_adfs):
    """*LEX examines the library directory."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("LEX") == "PASS", \
        f"*LEX failed:\n{dump_screen(bbc_adfs)}"
