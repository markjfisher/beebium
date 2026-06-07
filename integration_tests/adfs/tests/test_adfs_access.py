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

"""ADFS integration test: file access attributes and locking (*ACCESS)."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.screen import screen_contains, dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_access.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_access.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_default_access(bbc_adfs):
    """New files have default WR access."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("DEFAULT-WR") == "PASS", \
        f"Default access check failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_set_lock(bbc_adfs):
    """*ACCESS sets L attribute on a file."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("SET-LOCK") == "PASS", \
        f"*ACCESS L failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_delete_locked(bbc_adfs):
    """*DELETE on a locked file produces an error."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("DEL-LOCKED") == "PASS", \
        f"Delete of locked file did not error:\n{dump_screen(bbc_adfs)}"


def test_adfs_unlock_and_delete(bbc_adfs):
    """Removing lock then deleting succeeds."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("UNLOCK-DEL") == "PASS", \
        f"Unlock and delete failed:\n{dump_screen(bbc_adfs)}"
