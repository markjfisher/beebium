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

"""ADFS integration test: file create, delete, rename, save, and load."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.screen import dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_files.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_files.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_create_file(bbc_adfs):
    """OPENOUT creates a file and CLOSE# closes it."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("CREATE") == "PASS", \
        f"File creation failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_delete_file(bbc_adfs):
    """*DELETE removes a file."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("DELETE") == "PASS", \
        f"*DELETE failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_rename_file(bbc_adfs):
    """*RENAME changes a file's name."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("RENAME") == "PASS", \
        f"*RENAME failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_save_file(bbc_adfs):
    """*SAVE saves a memory region as a file."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("SAVE") == "PASS", \
        f"*SAVE failed:\n{dump_screen(bbc_adfs.memory)}"


def test_adfs_load_file(bbc_adfs):
    """*LOAD loads a file back into memory with correct contents."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"
    results = parse_results(bbc_adfs)
    assert results.get("LOAD") == "PASS", \
        f"*LOAD failed:\n{dump_screen(bbc_adfs.memory)}"
