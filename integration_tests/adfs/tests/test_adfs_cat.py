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

"""ADFS integration test: *CAT and *EX commands on a blank disc."""

from __future__ import annotations

import pytest

from beebium.client.screen import screen_contains, dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

from adfs_test_support import PROGRAMS_DIRPATH


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_cat.bas and build a DFS SSD."""
    source = (PROGRAMS_DIRPATH / "test_cat.bas").read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_cat_on_blank_disc(bbc_adfs):
    """Run *CAT and *EX on a blank ADFS hard disc."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"

    results = parse_results(bbc_adfs)
    assert results.get("CAT-ROOT") == "PASS", \
        f"*CAT failed:\n{dump_screen(bbc_adfs)}"
    assert results.get("EX-ROOT") == "PASS", \
        f"*EX failed:\n{dump_screen(bbc_adfs)}"

    # Verify the root directory marker appears on screen
    assert screen_contains(bbc_adfs, "$"), \
        f"Root directory '$' not found on screen:\n{dump_screen(bbc_adfs)}"
